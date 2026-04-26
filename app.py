from flask import Flask, request
import numpy as np
import cv2

app = Flask(__name__)

# =====================================================================
# הקשחת שרת (Server Hardening)
# =====================================================================

# 1. פתרון ל-DoS וניהול זיכרון: הגבלת גודל הבקשה
# אנחנו מגבילים את גודל הקובץ הנכנס ל-1 מגה-בייט (1 * 1024 * 1024 בתים).
# תמונת QVGA באיכות בינונית מה-ESP32-CAM שוקלת לרוב פחות מ-50KB.
# אם הבקשה תהיה גדולה מ-1MB, Flask תדחה אותה אוטומטית לפני הטעינה ל-RAM (קוד 413).
app.config['MAX_CONTENT_LENGTH'] = 1 * 1024 * 1024 

# 2. פתרון לבקרת גישה: הגדרת מפתח סודי (Shared Secret)
# רק מי שמכיר את המפתח הזה וישלח אותו בכותרות יוכל להפעיל את הרשת הנוירונית.
SECRET_ROBOT_KEY = "MediMate_Super_Secret_Key_2026"

# אתחול המודל פעם אחת
hog = cv2.HOGDescriptor()
hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())

@app.route('/ping', methods=['GET'])
def ping():
    return "Server is Awake", 200

@app.route('/upload', methods=['POST'])
def upload():
    # ---------------------------------------------------------
    # שכבת אבטחה: אימות זהות (Authentication)
    # ---------------------------------------------------------
    # משיכת המפתח מתוך הכותרות (Headers) של בקשת ה-HTTP
    client_key = request.headers.get('X-Robot-Key')
    if client_key != SECRET_ROBOT_KEY:
        # חסימת גישה במיידי, החזרת קוד 401 Unauthorized
        return "UNAUTHORIZED_ACCESS", 401

    # ---------------------------------------------------------
    # שכבת בטיחות: מניעת קריסות (Error Handling)
    # ---------------------------------------------------------
    try:
        file_data = request.data
        if not file_data:
            return "NO_DATA", 400

        # ניסיון להמיר את המידע למערך. אם המידע פגום, זה יזרוק שגיאה שנתפוס ב-except
        nparr = np.frombuffer(file_data, np.uint8)
        img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

        # בדיקה נוספת: לפעמים frombuffer עובד, אבל imdecode נכשל כי התמונה חתוכה
        if img is None or img.size == 0:
            return "CORRUPTED_IMAGE", 400

    except Exception as e:
        # תפסנו את השגיאה! השרת לא קורס. אנחנו מחזירים קוד שגיאה לבקר הראשי.
        print(f"Exception during image parsing: {str(e)}")
        return "PARSING_ERROR", 400

    # ---------------------------------------------------------
    # אזור העיבוד (Processing) - מתבצע רק אם עברנו את כל ההגנות
    # ---------------------------------------------------------
    person_found = detect_person(img)

    if person_found:
        return "PERSON_DETECTED", 200
    else:
        return "NO_PERSON", 200

def detect_person(image):
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
    boxes, weights = hog.detectMultiScale(gray, winStride=(8, 8), padding=(4, 4), scale=1.05)
    
    if len(boxes) > 0:
        return True
    return False

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
