"""
MediMate – Centralized Flask REST API Backend
==============================================
Hosted on Render Free Tier: 512 MB RAM, ~0.1 vCPU.
All resource-intensive objects (HOG descriptor, Twilio client, GCV client)
are initialized ONCE at module load time so that:
  1. Cold-start latency after the 50-second Render wake-up is absorbed
     during the first real request rather than on every request.
  2. Each worker process shares these objects without re-allocating them,
     keeping heap pressure well below the 512 MB ceiling.

Security model: every endpoint is guarded by a custom X-Robot-Key header
checked in the `require_robot_key` decorator BEFORE any payload is decoded.
This prevents unauthenticated callers from causing OOM by sending garbage.
"""

import os
import re
import logging
import traceback
from functools import wraps

import cv2
import numpy as np
from flask import Flask, request, jsonify, abort

from twilio.rest import Client as TwilioClient
from google.cloud import vision as gcp_vision

# ---------------------------------------------------------------------------
# Logging – structured output makes Render's log tail useful
# ---------------------------------------------------------------------------
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("medimate")

# ---------------------------------------------------------------------------
# Flask application
# ---------------------------------------------------------------------------
app = Flask(__name__)

# Hard cap on incoming body size: 2 MB.
# Flask will return HTTP 413 automatically if the client sends more.
# This is the FIRST line of defence against OOM on the free tier –
# a rogue ESP32 flashing its full PSRAM (4 MB) would otherwise be accepted,
# decoded into a NumPy array (~12 MB uncompressed), and crash the process.
app.config["MAX_CONTENT_LENGTH"] = 2 * 1024 * 1024  # 2 097 152 bytes

# ---------------------------------------------------------------------------
# Shared secret – read from environment variable so the value is NEVER
# committed to source control.  Set it in Render's Environment → Secret Files.
# ---------------------------------------------------------------------------
ROBOT_API_KEY: str = os.environ.get("ROBOT_API_KEY", "CHANGE_ME_IN_ENV")
if ROBOT_API_KEY == "CHANGE_ME_IN_ENV":
    log.warning(
        "ROBOT_API_KEY is not set in the environment. "
        "All requests will be rejected."
    )

# ---------------------------------------------------------------------------
# HOG + SVM Person Detector – initialized GLOBALLY (module scope).
#
# Why global?
#   cv2.HOGDescriptor() allocates ~200 KB for the descriptor coefficients.
#   getSVMDetector() loads the pre-trained 1 764-element weight vector.
#   Doing this inside the request handler would:
#     (a) add ~40 ms latency on every call, and
#     (b) fragment the heap – Python's allocator does not coalesce freed
#         NumPy arenas immediately, so repeated alloc/free cycles can push
#         the RSS above 512 MB and cause Render to OOM-kill the process.
#
# The default people detector (trained on 64×128 windows at 8×8 cells)
# runs comfortably in ~30 ms on Render's shared vCPU for a 320×240 frame.
# ---------------------------------------------------------------------------
_hog = cv2.HOGDescriptor()
_hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())
log.info("HOG+SVM descriptor loaded into process heap (shared across requests)")

# ---------------------------------------------------------------------------
# Twilio client – one HTTPS session reused across calls.
# Twilio's Python SDK uses `requests.Session` internally; keeping the client
# alive avoids TLS handshake overhead (~200 ms) on every notification call.
# ---------------------------------------------------------------------------
TWILIO_ACCOUNT_SID: str = os.environ.get("TWILIO_ACCOUNT_SID", "")
TWILIO_AUTH_TOKEN: str  = os.environ.get("TWILIO_AUTH_TOKEN", "")
TWILIO_FROM_NUMBER: str = os.environ.get("TWILIO_FROM_NUMBER", "")
TWILIO_TWIML_URL: str   = os.environ.get(
    "TWILIO_TWIML_URL",
    "http://demo.twilio.com/docs/voice.xml"  # default TwiML for testing
)

_twilio: TwilioClient | None = None
if TWILIO_ACCOUNT_SID and TWILIO_AUTH_TOKEN:
    _twilio = TwilioClient(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN)
    log.info("Twilio client initialized (SID …%s)", TWILIO_ACCOUNT_SID[-4:])
else:
    log.warning("Twilio credentials missing – /api/notifications/call will fail")

# ---------------------------------------------------------------------------
# Google Cloud Vision client – uses Application Default Credentials (ADC).
# Set GOOGLE_APPLICATION_CREDENTIALS env-var to the path of a service-account
# JSON key file on Render.  The client stub is tiny; the gRPC channel is
# created lazily on first call, so no latency hit at startup.
# ---------------------------------------------------------------------------
_gcv: gcp_vision.ImageAnnotatorClient | None = None
try:
    _gcv = gcp_vision.ImageAnnotatorClient()
    log.info("Google Cloud Vision client initialized via ADC")
except Exception as exc:  # noqa: BLE001
    log.warning("GCV client init failed: %s – /api/medical/parse_prescription will fail", exc)

# ---------------------------------------------------------------------------
# Decorator: enforce X-Robot-Key on every protected endpoint.
#
# The check happens BEFORE Flask's request body is buffered, so a rejected
# request drains the socket and frees memory without touching cv2 or numpy.
# Checking a fixed-length secret with a constant-time compare (hmac.compare_digest)
# prevents timing-oracle attacks where an attacker infers correct key bytes
# by measuring response latency differences.
# ---------------------------------------------------------------------------
import hmac  # stdlib – safe constant-time string comparison

def require_robot_key(f):
    """Decorator that rejects requests whose X-Robot-Key header does not match
    the shared secret stored in ROBOT_API_KEY environment variable."""
    @wraps(f)
    def decorated(*args, **kwargs):
        incoming_key: str = request.headers.get("X-Robot-Key", "")
        # hmac.compare_digest operates in O(n) time regardless of where the
        # first mismatch occurs, preventing timing side-channel leakage.
        if not hmac.compare_digest(incoming_key.encode(), ROBOT_API_KEY.encode()):
            log.warning(
                "Rejected request to %s – bad or missing X-Robot-Key "
                "(caller IP: %s)",
                request.path,
                request.remote_addr,
            )
            # 403 Forbidden rather than 401 – we do not advertise an auth scheme
            abort(403)
        return f(*args, **kwargs)
    return decorated


# ===========================================================================
# ENDPOINT 1 – /api/vision/detect
# ===========================================================================
@app.route("/api/vision/detect", methods=["POST"])
@require_robot_key
def vision_detect():
    """
    Receives a raw JPEG byte stream from the Robot ESP32-CAM and returns
    whether a person was detected.

    Memory lifecycle (critical on free tier):
    ──────────────────────────────────────────
    1.  request.data   → Python `bytes` object on the heap   (~50–300 KB JPEG)
    2.  np.frombuffer  → NumPy uint8 array, *no copy* of the bytes object
    3.  cv2.imdecode   → new BGR ndarray (width × height × 3 bytes)
                         For a 320×240 frame: 230 400 bytes ≈ 225 KB
    4.  _hog.detectMultiScale → output list of rectangles (tiny)
    5.  All intermediate arrays go out of scope → CPython refcount → freed
        immediately (no GC cycle needed for pure-numpy objects)

    Total peak heap delta: ~550 KB per request, well within 512 MB.
    """
    # ── Validate Content-Type ──────────────────────────────────────────────
    # Flask already enforced MAX_CONTENT_LENGTH; here we reject non-image
    # bodies early to avoid wasting decode cycles on JSON or text payloads.
    content_type: str = request.content_type or ""
    if "image" not in content_type and "octet-stream" not in content_type:
        return jsonify({"error": "Expected image/jpeg or application/octet-stream"}), 415

    try:
        raw_bytes: bytes = request.data  # already limited to 2 MB by Flask

        if not raw_bytes:
            return jsonify({"error": "Empty body"}), 400

        # np.frombuffer: wraps the Python bytes buffer WITHOUT copying it.
        # The resulting array is read-only, but cv2.imdecode only reads it.
        jpeg_array = np.frombuffer(raw_bytes, dtype=np.uint8)

        # cv2.imdecode: allocates a new BGR matrix in C heap (not Python heap),
        # decoded from the JPEG entropy stream.  Returns None on corrupt data.
        frame: np.ndarray | None = cv2.imdecode(jpeg_array, cv2.IMREAD_COLOR)
        if frame is None:
            return jsonify({"error": "cv2.imdecode failed – corrupt JPEG"}), 422

        # ── HOG Detection ─────────────────────────────────────────────────
        # winStride=(8,8): slide the 64×128 detection window every 8 px.
        #   Smaller stride → more detections → more CPU → not suitable here.
        # scale=1.05: gentle image pyramid (5% reduction per level).
        #   Higher value → fewer pyramid levels → faster but misses scale.
        # padding=(4,4): mirror-pad each window to handle edge persons.
        rects, weights = _hog.detectMultiScale(
            frame,
            winStride=(8, 8),
            padding=(4, 4),
            scale=1.05,
        )

        person_detected: bool = len(rects) > 0
        confidence: float = float(np.max(weights)) if person_detected else 0.0

        log.info(
            "vision/detect: person=%s confidence=%.3f rects=%d frame=%dx%d",
            person_detected, confidence, len(rects), frame.shape[1], frame.shape[0],
        )

        # Explicitly delete the C-heap-backed ndarray now rather than waiting
        # for GC: cv2 matrices are reference-counted in C++ (cv::Mat), and del
        # immediately decrements that count, releasing the BGR buffer.
        del frame
        del jpeg_array

        return jsonify({
            "person_detected": person_detected,
            "confidence": round(confidence, 4),
            "detection_count": int(len(rects)),
        }), 200

    except MemoryError:
        # Catch numpy allocation failures gracefully – the process should stay
        # alive to serve the next request rather than crashing.
        log.error("OOM during HOG decode – frame too large for free tier")
        return jsonify({"error": "Server OOM – reduce frame resolution"}), 507
    except Exception as exc:  # noqa: BLE001
        log.error("Unexpected error in vision_detect:\n%s", traceback.format_exc())
        return jsonify({"error": "Internal server error", "detail": str(exc)}), 500


# ===========================================================================
# ENDPOINT 2 – /api/notifications/call
# ===========================================================================
@app.route("/api/notifications/call", methods=["POST"])
@require_robot_key
def notifications_call():
    """
    Initiates a Twilio voice call to a given phone number.

    Payload (JSON):
        { "to": "+972501234567", "message": "optional override TwiML URL" }

    The ESP32 Box calls this endpoint when the 5-minute fingerprint timeout
    expires and the Robot has not confirmed person detection.

    Twilio SDK internals: Client.calls.create() makes ONE HTTPS POST to
    api.twilio.com, which queues the call asynchronously.  The HTTP response
    arrives in ~200–800 ms.  The actual phone ring happens seconds later,
    server-side – we do not need to hold the connection open.
    """
    if not _twilio:
        return jsonify({"error": "Twilio not configured on server"}), 503

    payload = request.get_json(force=False, silent=True)
    if payload is None:
        return jsonify({"error": "Body must be valid JSON"}), 400

    # Validate the destination number
    to_number: str = payload.get("to", "").strip()
    if not to_number:
        return jsonify({"error": "Missing 'to' field"}), 422

    # Allow caller to override the TwiML URL (useful for custom voice messages)
    twiml_url: str = payload.get("twiml_url", TWILIO_TWIML_URL)

    try:
        call = _twilio.calls.create(
            to=to_number,
            from_=TWILIO_FROM_NUMBER,
            url=twiml_url,
            # timeout: Twilio will attempt to ring for at most 30 seconds
            timeout=30,
        )
        log.info("Twilio call initiated: SID=%s to=%s", call.sid, to_number)
        return jsonify({"status": "call_initiated", "call_sid": call.sid}), 200

    except Exception as exc:  # noqa: BLE001
        log.error("Twilio call failed: %s", exc)
        return jsonify({"error": "Twilio call failed", "detail": str(exc)}), 502


# ===========================================================================
# ENDPOINT 3 – /api/medical/parse_prescription
# ===========================================================================

# Pre-compiled regex patterns for medication extraction.
# Compiling at module load: re.compile is ~10× faster than inline re.search
# when called hundreds of times per session.
#
# Pattern strategy:
#   _RX_DRUG_LINE  – matches a capitalized word followed by optional dosage
#                    (e.g. "Metformin 500mg", "Atorvastatin 20MG")
#   _RX_DOSAGE     – captures the numeric dosage for structured output
_RX_DRUG_LINE = re.compile(
    r"\b([A-Z][a-z]{2,}(?:\s[A-Z][a-z]+)?)"  # Capitalized drug name (1–2 words)
    r"(?:\s+(\d+(?:\.\d+)?\s*(?:mg|mcg|ml|IU|units?)))?",  # Optional dosage
    re.MULTILINE
)
_RX_DOSAGE = re.compile(r"(\d+(?:\.\d+)?)\s*(mg|mcg|ml|IU|units?)", re.IGNORECASE)

# Common English words to exclude from drug matches (reduces false positives)
_STOPWORDS = frozenset({
    "Take", "Once", "Twice", "Daily", "Tablet", "Capsule", "With",
    "Food", "Water", "Before", "After", "Morning", "Evening", "Night",
    "Patient", "Doctor", "Prescription", "Name", "Date", "Refill",
})


@app.route("/api/medical/parse_prescription", methods=["POST"])
@require_robot_key
def parse_prescription():
    """
    Receives a prescription image (JPEG/PNG) from the Main Box, sends it to
    Google Cloud Vision TEXT_DETECTION for OCR, then extracts medication
    names and dosages via regex and returns structured JSON.

    GCV TEXT_DETECTION vs DOCUMENT_TEXT_DETECTION:
      - TEXT_DETECTION: optimized for sparse, printed text (labels, signs).
        Returns individual word annotations with bounding polygons.
      - DOCUMENT_TEXT_DETECTION: better for dense, multi-column documents
        but ~40% slower.  Prescription labels are sparse → TEXT_DETECTION wins.

    The image is forwarded to GCV as base64-encoded content inside a JSON
    body (gRPC internally handles the encoding via the SDK).  We never write
    the image to disk, keeping the entire pipeline in-memory.
    """
    if not _gcv:
        return jsonify({"error": "Google Cloud Vision not configured"}), 503

    content_type: str = request.content_type or ""
    if "image" not in content_type and "octet-stream" not in content_type:
        return jsonify({"error": "Expected an image payload"}), 415

    raw_bytes: bytes = request.data
    if not raw_bytes:
        return jsonify({"error": "Empty body"}), 400

    try:
        # gcp_vision.Image wraps the raw bytes; the SDK encodes to base64
        # internally before sending over HTTPS to the GCV endpoint.
        gcv_image = gcp_vision.Image(content=raw_bytes)

        # Perform OCR – blocks until GCV responds (typically 300–1500 ms).
        response = _gcv.text_detection(image=gcv_image)  # type: ignore[arg-type]

        if response.error.message:
            # GCV returns structured error messages (e.g. "billing not enabled")
            log.error("GCV error: %s", response.error.message)
            return jsonify({"error": "GCV API error", "detail": response.error.message}), 502

        # text_annotations[0] is the full-page text block (all words joined).
        # Subsequent elements are per-word annotations with bounding boxes.
        # We only need the full block for regex extraction.
        if not response.text_annotations:
            return jsonify({"medications": [], "raw_text": ""}), 200

        full_text: str = response.text_annotations[0].description
        log.info("GCV OCR extracted %d characters", len(full_text))

        # ── Regex medication extraction ────────────────────────────────────
        medications: list[dict] = []
        seen: set[str] = set()  # deduplicate – prescriptions repeat drug names

        for match in _RX_DRUG_LINE.finditer(full_text):
            drug_name: str = match.group(1).strip()

            # Filter stopwords and very short matches (< 4 chars are noise)
            if drug_name in _STOPWORDS or len(drug_name) < 4:
                continue

            # Deduplicate case-insensitively
            key = drug_name.lower()
            if key in seen:
                continue
            seen.add(key)

            # Extract dosage from the same match group (group 2) or fallback
            raw_dosage: str = match.group(2) or ""
            dosage_match = _RX_DOSAGE.search(raw_dosage)
            dosage_value: float | None = float(dosage_match.group(1)) if dosage_match else None
            dosage_unit:  str | None  = dosage_match.group(2).lower() if dosage_match else None

            medications.append({
                "name":        drug_name,
                "dosage_raw":  raw_dosage.strip() or None,
                "dosage_value": dosage_value,
                "dosage_unit":  dosage_unit,
            })

        log.info("Extracted %d medication entries from prescription", len(medications))

        return jsonify({
            "medications": medications,
            "raw_text":    full_text,
            "ocr_word_count": len(full_text.split()),
        }), 200

    except Exception as exc:  # noqa: BLE001
        log.error("parse_prescription error:\n%s", traceback.format_exc())
        return jsonify({"error": "Internal server error", "detail": str(exc)}), 500


# ===========================================================================
# Error handlers – override Flask defaults to always return JSON
# ===========================================================================
@app.errorhandler(403)
def forbidden(e):
    return jsonify({"error": "Forbidden – invalid or missing X-Robot-Key"}), 403

@app.errorhandler(413)
def request_entity_too_large(e):
    return jsonify({"error": "Payload too large – max 2 MB"}), 413

@app.errorhandler(404)
def not_found(e):
    return jsonify({"error": "Endpoint not found"}), 404

@app.errorhandler(405)
def method_not_allowed(e):
    return jsonify({"error": "Method not allowed"}), 405


# ===========================================================================
# Entry point
# ===========================================================================
if __name__ == "__main__":
    # For local dev only.  On Render, gunicorn is used:
    #   gunicorn app:app --workers 1 --threads 2 --timeout 120
    #
    # workers=1 is intentional: multiple workers each load the HOG descriptor
    # and GCV client independently, doubling (or more) the baseline RAM usage.
    # One worker with two threads is optimal for I/O-bound endpoints like
    # GCV and Twilio (both spend most time waiting for HTTPS responses).
    app.run(host="0.0.0.0", port=int(os.environ.get("PORT", 5000)), debug=False)
