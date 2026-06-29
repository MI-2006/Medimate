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
@app.route('/', methods=['GET'])
def home():
    return "MediMate Server is Awake and Running! 🚀"
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
#
# This endpoint now performs TWO extraction passes over the OCR text:
#   1. Medication identification (drug name + dosage) – unchanged strategy.
#   2. Scheduling heuristics (proposed_schedule) – NEW.
#
# The scheduling engine is a pure Regex + arithmetic heuristic layer.
# No ML model, tokenizer, or vector store is loaded, so the memory delta
# for this feature is effectively zero – it reuses the same 512 MB budget
# already allocated to HOG/GCV/Twilio at module import time.
#
# Clinical rationale for every rule is documented inline below, since this
# output is shown to a human caregiver for confirmation before it is ever
# written to Firebase and used to unlock a physical medication compartment.
# ---------------------------------------------------------------------------

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

# ---------------------------------------------------------------------------
# Scheduling heuristics – ALL patterns compiled at module scope (never inside
# the request handler or the helper function) so they are parsed by the
# regex engine exactly once per process lifetime, not once per request.
# ---------------------------------------------------------------------------

# Rule 4 – PRN / "as needed" detection.
# Clinically, a PRN order must NEVER receive a fixed clock-time schedule –
# doing so would cause the box to alert/dispense on a timer for a medication
# the patient may not need that day (e.g. PRN pain relief), which is both
# clinically wrong and erodes the patient's trust in the alerting system.
_RX_PRN = re.compile(
    r"\b(?:as\s+needed|p\.?\s*r\.?\s*n\.?|if\s+(?:pain|needed|required))\b",
    re.IGNORECASE,
)

# Rule 2 – strict numeric interval dosing (e.g. "Every 8 hours" for antibiotics).
# These orders are pharmacokinetically driven (trough/peak blood levels), so
# – unlike "3 times a day" – the interval must be taken LITERALLY rather than
# redistributed into waking hours, even if that means a 00:00 dose.
_RX_EVERY_X_HOURS = re.compile(
    r"every\s+(\d{1,2})\s*(?:h|hr|hrs|hour|hours)\b",
    re.IGNORECASE,
)

# Rule 1 – "N times a day" / "once daily" / "twice daily" style frequency.
# Accepts both digits ("3 times a day") and spelled-out frequency words
# ("once", "twice", "three times") because OCR of printed labels commonly
# renders both forms depending on the pharmacy's label template.
_RX_TIMES_A_DAY = re.compile(
    r"\b(once|twice|thrice|one|two|three|four|\d{1,2})"
    r"\s*(?:times?)?\s*(?:a|per)?\s*(?:day|daily)\b",
    re.IGNORECASE,
)
_FREQ_WORD_TO_INT: dict[str, int] = {
    "once": 1, "one": 1,
    "twice": 2, "two": 2,
    "thrice": 3, "three": 3,
    "four": 4,
}

# Rule 3 – daily anchor keywords.
_RX_MORNING = re.compile(r"\bmorning\b", re.IGNORECASE)
_RX_EVENING = re.compile(r"\b(?:evening|bedtime|night)\b", re.IGNORECASE)
_RX_WITH_FOOD = re.compile(
    r"\b(?:with\s+(?:food|meals?)|before\s+meals?|after\s+meals?)\b",
    re.IGNORECASE,
)

# ---------------------------------------------------------------------------
# Scheduling constants.
#
# "Waking hours" window (08:00–20:00) is a deliberate clinical/UX choice:
# distributing N-times-a-day doses mathematically across a full 24h clock
# (24 / N) would place doses at implausible hours (e.g. 3×/day → 00:00,
# 08:00, 16:00), waking an elderly patient at midnight. Anchoring the
# window to typical waking hours keeps every *frequency-based* dose inside
# hours the patient is realistically awake and near the box.
# ---------------------------------------------------------------------------
_WAKING_START_HOUR: float = 8.0   # 08:00
_WAKING_END_HOUR:   float = 20.0  # 20:00
_ANCHOR_MORNING:    str = "08:00"
_ANCHOR_EVENING:    str = "21:00"  # bedtime anchor sits slightly after the
                                    # waking-hours window, matching typical
                                    # elderly bedtime routines.
_MEAL_ANCHORS: tuple[str, str, str] = ("08:00", "13:00", "19:00")

# Sanity clamp: prescriptions requesting more than 6 doses/day are almost
# always an OCR misread (e.g. a dosage number captured as a frequency), so
# we clamp rather than generate a schedule that would spam the caregiver.
_MAX_PLAUSIBLE_DOSES_PER_DAY: int = 6


def _format_hhmm(hour_value: float) -> str:
    """Convert a fractional hour (e.g. 14.5) into a zero-padded 'HH:MM'
    string, wrapping correctly past midnight (24h clock, modulo 1440 min)."""
    total_minutes = round(hour_value * 60.0) % (24 * 60)
    hours, minutes = divmod(total_minutes, 60)
    return f"{hours:02d}:{minutes:02d}"


def _distribute_waking_hours(dose_count: int) -> list[str]:
    """Spread `dose_count` doses evenly across the 08:00–20:00 waking-hours
    window (Requirement #1). A single dose is always anchored to 08:00
    (morning) rather than the window midpoint, matching standard once-daily
    prescribing convention."""
    dose_count = max(1, min(dose_count, _MAX_PLAUSIBLE_DOSES_PER_DAY))
    if dose_count == 1:
        return [_format_hhmm(_WAKING_START_HOUR)]
    span_hours = _WAKING_END_HOUR - _WAKING_START_HOUR
    step_hours = span_hours / (dose_count - 1)
    return [
        _format_hhmm(_WAKING_START_HOUR + (i * step_hours))
        for i in range(dose_count)
    ]


def _distribute_strict_interval(interval_hours: int) -> list[str]:
    """Compute literal 'every X hours' clock times starting at 08:00
    (Requirement #2). Number of doses = floor(24 / interval); e.g. every
    8 hours → 3 doses at 08:00 / 16:00 / 00:00. Unlike _distribute_waking_hours
    this deliberately DOES roll past midnight, because interval dosing is
    driven by drug half-life, not patient wakefulness."""
    interval_hours = max(1, min(interval_hours, 24))
    dose_count = max(1, 24 // interval_hours)
    return [
        _format_hhmm((_WAKING_START_HOUR + (i * interval_hours)) % 24.0)
        for i in range(dose_count)
    ]


def _resolve_frequency_count(token: str) -> int | None:
    """Normalize a frequency token ('twice', '3', 'three times') captured by
    _RX_TIMES_A_DAY into an integer dose count, or None if unrecognized."""
    token = token.strip().lower()
    if token.isdigit():
        return int(token)
    return _FREQ_WORD_TO_INT.get(token)


def parse_schedule_heuristics(ocr_text: str) -> dict:
    """
    Derive a proposed daily dosing schedule from free-text prescription
    instructions using a fixed priority chain of clinical heuristics.

    Priority order (highest → lowest) and the reasoning behind it:
      1. PRN / as-needed   – MUST short-circuit everything else; a fixed
                              schedule for an as-needed drug is a safety bug.
      2. Every X hours      – pharmacokinetic interval dosing overrides
                              generic frequency wording if both appear
                              (e.g. "Take twice daily, every 12 hours").
      3. Morning + Evening  – explicit dual anchor implies BID dosing even
                              if no numeric frequency was OCR'd.
      4. N times a day      – generic frequency, redistributed into waking
                              hours (never a literal 24/N division).
      5. With food / meals  – anchored to typical meal times.
      6. Morning only       – single anchor.
      7. Evening only       – single anchor (bedtime).
      8. No match           – return an empty schedule rather than guessing;
                              the caregiver app must prompt for manual entry
                              instead of silently proposing an unsupported
                              time (fail-safe default, not fail-silent).

    Args:
        ocr_text: raw OCR text associated with a single medication line
                   (the drug's own line plus a small trailing context
                   window – see parse_prescription()).

    Returns:
        dict with keys: proposed_schedule (list[str]), is_as_needed (bool),
        schedule_source (str) – the latter is diagnostic metadata for
        logging/debugging and is safe for the frontend to ignore.
    """
    text_lower = ocr_text.lower()

    # Rule 4 (highest priority) – PRN / as-needed.
    if _RX_PRN.search(text_lower):
        return {
            "proposed_schedule": [],
            "is_as_needed": True,
            "schedule_source": "prn",
        }

    # Rule 2 – strict interval dosing (e.g. antibiotic q8h).
    every_match = _RX_EVERY_X_HOURS.search(text_lower)
    if every_match:
        interval_hours = int(every_match.group(1))
        return {
            "proposed_schedule": _distribute_strict_interval(interval_hours),
            "is_as_needed": False,
            "schedule_source": "interval",
        }

    has_morning = bool(_RX_MORNING.search(text_lower))
    has_evening = bool(_RX_EVENING.search(text_lower))

    # Rule 3 – both morning and evening anchors present → BID schedule.
    if has_morning and has_evening:
        return {
            "proposed_schedule": [_ANCHOR_MORNING, _ANCHOR_EVENING],
            "is_as_needed": False,
            "schedule_source": "anchor_morning_evening",
        }

    # Rule 1 – generic "N times a day" frequency wording.
    times_match = _RX_TIMES_A_DAY.search(text_lower)
    if times_match:
        dose_count = _resolve_frequency_count(times_match.group(1))
        if dose_count:
            return {
                "proposed_schedule": _distribute_waking_hours(dose_count),
                "is_as_needed": False,
                "schedule_source": "times_per_day",
            }

    # Rule 3 (meal variant) – "with food" / "with meals" instructions.
    if _RX_WITH_FOOD.search(text_lower):
        return {
            "proposed_schedule": list(_MEAL_ANCHORS),
            "is_as_needed": False,
            "schedule_source": "meals",
        }

    # Rule 3 (single anchor) – morning only.
    if has_morning:
        return {
            "proposed_schedule": [_ANCHOR_MORNING],
            "is_as_needed": False,
            "schedule_source": "anchor_morning",
        }

    # Rule 3 (single anchor) – evening / bedtime only.
    if has_evening:
        return {
            "proposed_schedule": [_ANCHOR_EVENING],
            "is_as_needed": False,
            "schedule_source": "anchor_evening",
        }

    # Rule 8 – nothing recognized. Fail-safe: leave schedule empty so the
    # caregiver app forces manual confirmation rather than dispensing at a
    # clinically-unsupported guessed time.
    return {
        "proposed_schedule": [],
        "is_as_needed": False,
        "schedule_source": "unrecognized",
    }


@app.route("/api/medical/parse_prescription", methods=["POST"])
@require_robot_key
def parse_prescription():
    """
    Receives a prescription image (JPEG/PNG) from the Main Box, sends it to
    Google Cloud Vision TEXT_DETECTION for OCR, then extracts medication
    names, dosages, and a heuristic dosing schedule via regex, returning
    structured JSON for the caregiver app to confirm before it is written
    to Firebase.

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
        
        # --- תיקון: מחיקת המילה Patient ושם המטופל (שתי מילים) מהטקסט הגולמי ---
        full_text = re.sub(r"(?i)Patient:\s*[A-Za-z]+\s+[A-Za-z]+", "", full_text)
        
        log.info("GCV OCR extracted %d characters", len(full_text))
        log.info("GCV OCR extracted %d characters", len(full_text))

        # ── Regex medication + schedule extraction ─────────────────────────
        # OCR text is processed line-by-line (rather than as one blob) so
        # that each medication can be paired with the instruction text that
        # is physically printed nearest to it on the label. Pharmacy labels
        # typically place dosing instructions on the line immediately
        # following the drug name/dosage line, so we build a small
        # 2-line "context window" (current line + next line) per match.
        medications: list[dict] = []
        seen: set[str] = set()  # deduplicate – prescriptions repeat drug names
        lines: list[str] = full_text.splitlines()

        for line_index, line in enumerate(lines):
            for match in _RX_DRUG_LINE.finditer(line):
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
                dosage_value: float | None = (
                    float(dosage_match.group(1)) if dosage_match else None
                )
                dosage_unit: str | None = (
                    dosage_match.group(2).lower() if dosage_match else None
                )

                # Build the instruction context window: current line plus the
                # following line (guarded against IndexError at EOF).
                context_lines = [line]
                if line_index + 1 < len(lines):
                    context_lines.append(lines[line_index + 1])
                instruction_context: str = " ".join(context_lines).strip()

                schedule_info = parse_schedule_heuristics(instruction_context)

                medications.append({
                    "name": drug_name,
                    # Human-readable combined dosage string for direct UI display.
                    "dosage": raw_dosage.strip() or None,
                    "dosage_raw": raw_dosage.strip() or None,
                    "dosage_value": dosage_value,
                    "dosage_unit": dosage_unit,
                    "proposed_schedule": schedule_info["proposed_schedule"],
                    "is_as_needed": schedule_info["is_as_needed"],
                    "schedule_source": schedule_info["schedule_source"],
                    "instruction_raw_text": instruction_context,
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