MediMate
Medical independence, safe and smart.

MediMate is an advanced autonomous system designed to allow the elderly population to manage their medication routine independently, safely and accurately. The system combines a smart medicine box and a mobile robotic unit, which provide a complete protective envelope – even for those who live alone.

The system is based on three main pillars:

The Hub: The stationary unit that manages the inventory, identifies the user via fingerprint, and physically verifies proper medication intake using optical sensors.

The Scout: A mobile unit equipped with a thermal camera. As soon as the box detects that the user has not taken their medication, the robot goes out to locate them throughout the house and alerts them visually and audibly until they have successfully taken it.

The Interface: Allows family and caregivers to monitor medication compliance, manage inventory and upload doctor's prescriptions.

Project Status: Under Development
The project is currently in active development.

Development focus: Building the synchronization logic between the box and the robot, and implementing safety mechanisms (fingerprint authentication).

Architecture: The system relies on an ESP32 controller that manages complex asynchronous communication, using advanced libraries for data processing (ArduinoJson) and user interface management (TFT_eSPI).

Key technologies
Microcontroller: ESP32 (parallel task management).

Sensors: Thermal camera (AMG8833), capacitive fingerprint recognition, ultrasonic distance sensors.

Connectivity: WiFi / Bluetooth (BLE).

Software Stack: C++ (Arduino Framework), JSON Handling, WebSockets/HTTP.

About the project
This project was developed within the framework of a laboratory in autonomous systems. It combines technological innovation (robotics, embedded systems, NLP/Data Analysis) with a critical human need – improving the quality of life of the elderly and preventing life-threatening medical errors.
