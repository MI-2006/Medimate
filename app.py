from flask import Flask, request, jsonify
import numpy as np
import cv2 # ספריית OpenCV לעיבוד תמונה

app = Flask(__name__)

@app.route('/ping', methods=['GET'])
def ping():
    # נתיב ההתעוררות - הקופסה תקרא לנתיב הזה
    return "Server is Awake", 200

@app.route('/upload', methods=['POST'])
def upload():
    # קבלת הנתונים הבינאריים מה-ESP32-CAM
    file_data = request.data
    if not file_data:
        return "No data received", 400

    # המרה של מערך הבתים לתמונה ש-OpenCV מבין ללא שמירה בדיסק
    nparr = np.frombuffer(file_data, np.uint8)
    img = cv2.imdecode(nparr, cv2.imread_color)

    # כאן יבוא אלגוריתם זיהוי האדם שלך
    person_found = detect_person(img) 

    if person_found:
        return "PERSON_DETECTED", 200
    else:
        return "NO_PERSON", 200

def detect_person(frame):
    # כאן תטמיעי מודל (כמו YOLOv8-tiny או HOG)
    # לצורך הבדיקה הראשונית, נחזיר True
    return True

if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000)