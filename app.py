from flask import Flask, request
import numpy as np
import cv2

app = Flask(__name__)

# =====================================================================
# אתחול גלובלי (Singleton Pattern)
# =====================================================================
# הנקודה הקריטית ביותר: אנחנו מאתחלים את מודל הזיהוי פעם אחת בלבד!
# אם היינו שמים את השורות האלו בתוך פונקציית הניתוב, השרת היה מקצה 
# זיכרון מחדש לכל תמונה שמגיעה, מה שהיה גורם לקריסת השרת ולזמן תגובה איטי.
hog = cv2.HOGDescriptor()
hog.setSVMDetector(cv2.HOGDescriptor_getDefaultPeopleDetector())


@app.route('/ping', methods=['GET'])
def ping():
    # נתיב ההתעוררות - אסור למחוק אותו! (זה מה שהקופסה קוראת לו)
    return "Server is Awake", 200


@app.route('/upload', methods=['POST'])
def upload():
    # 1. קבלת הזרם הבינארי הגולמי שמגיע מה-ESP32-CAM
    file_data = request.data
    if not file_data:
        return "No data received", 400

    # 2. המרה של רצף הבתים (Bytes) למערך חד-ממדי ש-NumPy יודע לעבוד איתו
    # הפעולה הזו קורית ישירות בזיכרון ה-RAM (In-memory) ללא כתיבה לדיסק! 
    # זה חוסך Latency עצום של I/O (קלט/פלט).
    nparr = np.frombuffer(file_data, np.uint8)

    # 3. פענוח המערך לכדי מטריצת פיקסלים (תמונה) של OpenCV
    # cv2.imread_color מפענח את התמונה למרחב צבעים BGR
    img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)

    # הגנת קריסה: אם המצלמה שלחה קובץ פגום או חתוך שאינו תמונה תקינה
    if img is None:
        return "ERROR_INVALID_IMAGE", 400

    # 4. הפעלת מנוע זיהוי האדם
    person_found = detect_person(img)

    # 5. החזרת התשובה לבקר (ברובוט שלך, הפונקציה processDecision מחכה לטקסט הזה בדיוק)
    if person_found:
        return "PERSON_DETECTED", 200
    else:
        return "NO_PERSON", 200


def detect_person(image):
    """
    פונקציה המקבלת מטריצת תמונה ומחזירה אמת/שקר אם זוהה אדם.
    נכתבה מתוך דגש חמור על חיסכון במשאבי שרת (CPU cycles).
    """
    
    # שלב א': אופטימיזציה של מידע הצבע
    # זיהוי צורות (HOG) עובד על שינויי ניגודיות (Gradients) ולא על צבעים.
    # המרה לשחור-לבן (Grayscale) חותכת את כמות המידע לעיבוד בשליש (מ-3 ערוצים לערוץ 1).
    # זה מוריד את זמן העיבוד של המעבד בשרת באופן משמעותי.
    gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)

    # שלב ב': התמודדות עם כיווניות המצלמה (Rotation)
    # אם המצלמה מותקנת הפוך על הרובוט, האלגוריתם לא יזהה אדם כי הוא מחפש ראש למעלה ורגליים למטה.
    # אם המצלמה שלך הפוכה מכנית, שחררי את ההערה מהשורה הבאה:
    # gray = cv2.rotate(gray, cv2.ROTATE_180)

    # שלב ג': הפעלת האלגוריתם עצמו (detectMultiScale)
    # winStride=(8,8): "חלון הסריקה" קופץ ב-8 פיקסלים בכל פעם. 
    #   הגדלת המספר (למשל 16,16) תהפוך את הקוד למהיר יותר אך פחות מדויק.
    # padding=(4,4): שוליים שעוזרים לזהות אנשים בקצוות התמונה.
    # scale=1.05: האלגוריתם מכווץ את התמונה ב-5% בכל איטרציה כדי למצוא אנשים בגדלים שונים.
    #   הגדלת ה-scale (למשל 1.1) תאיץ את השרת על חשבון פספוס של אנשים רחוקים.
    boxes, weights = hog.detectMultiScale(gray, winStride=(8, 8), padding=(4, 4), scale=1.05)

    # שלב ד': קבלת החלטה
    # הפונקציה מחזירה 'רשימה' של קופסאות (Bounding Boxes) שבהן זוהו אנשים.
    # אם הרשימה גדולה מ-0, סימן שמצאנו לפחות אדם אחד.
    if len(boxes) > 0:
        return True
    else:
        return False

# =====================================================================
# הפעלת השרת 
# (שורות אלו רלוונטיות רק אם מריצים לוקאלית, Render מפעיל דרך Gunicorn)
# =====================================================================
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)
