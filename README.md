Flow chart sistem timbagan otomatis

void setup() {
•	Inisialisasi semua komponen:
    	 - WiFi Manager
    	- Display (U8G2)
    	- Serial (RS232 untuk timbangan, RS485 untuk RFID)
   	 - RTC
    	- Relay dan Tombol
    	- Semua Manager instances
}

void loop() {
•	Update berjalan terus menerus tanpa blocking:
   	- WiFi Connection
    	- Weight Reading (setiap 100ms)
   	 - HTTP Client
    	- System State Machine
   	- Response Display
}

<img width="1606" height="1441" alt="deepseek_mermaid_20250901_79a82b" src="https://github.com/user-attachments/assets/d84abd75-6b32-42c8-9489-2ab6b8226aac" />
<img width="1628" height="2586" alt="deepseek_mermaid_20250901_4a4791" src="https://github.com/user-attachments/assets/3f107313-99a9-4bdf-92de-cfb787789bef" />
<img width="1532" height="1626" alt="deepseek_mermaid_20250901_9413da" src="https://github.com/user-attachments/assets/fe814c30-9b4e-41fb-b40c-4de182681aa8" />
<img width="2295" height="2010" alt="deepseek_mermaid_20250901_0f5520" src="https://github.com/user-attachments/assets/96b84121-0055-41e7-acdc-afe11b57ef39" />
<img width="1532" height="1674" alt="deepseek_mermaid_20250901_3118e3" src="https://github.com/user-attachments/assets/dcb8fd08-2b83-4833-9400-51439bd125e5" />
