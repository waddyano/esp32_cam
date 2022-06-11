# ESP32 Camera app

My app for a ESP32 Camera board. Built with esp-idf.

Uses httpd work queue to make response async and allow multiple clients for better behavior than most apps.
Supports OTA update via the UI. Currently no authorization needed so only expose to your local network.
