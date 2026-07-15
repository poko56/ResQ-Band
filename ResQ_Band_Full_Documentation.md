# 🛟 คู่มือฉบับสมบูรณ์: ResQ-Band
**The IoT Wearable Emergency Localization and Survival Triage System**
*ระบบกำไลข้อมืออัจฉริยะชี้เป้าและคัดกรองผู้รอดชีวิตใต้ซากอาคารถล่มสำหรับงาน US&R*

---

## 1. บทนำและจุดเด่นของระบบ (Introduction & Features)

จากปัญหาอุบัติเหตุอาคารและสิ่งก่อสร้างถล่ม **"เวลาทองคำ (Golden Hour)"** คือสิ่งสำคัญที่สุดในการช่วยชีวิต ทว่าโครงสร้างคอนกรีตและเหล็กเส้นมักดักจับและสะท้อนคลื่นวิทยุทั่วไป ทำให้ระบบระบุตำแหน่งมาตรฐานไม่สามารถทำงานได้ 

**ResQ-Band** คือโปรเจกต์ Open-Source ฮาร์ดแวร์และซอฟต์แวร์ที่ออกแบบมาเพื่อแก้ไขปัญหานี้โดยเฉพาะ โดยผสานเทคโนโลยีคลื่นความถี่ 2 รูปแบบเข้าด้วยกัน คือ **Ultra-Wideband (UWB)** และ **LoRa (433 MHz)**

### จุดเด่นหลัก (Key Features)
* 📍 **Precision Pre-Collapse Tracking:** ใช้คลื่น UWB ประเมินพิกัดแบบ Real-time ระดับเซนติเมตรขณะอาคารยังไม่ถล่ม (Smart Zone Attendance)
* 💥 **The Fall Trigger:** อัลกอริทึมตรวจจับสภาวะตกอิสระ (Free-fall) หรือแรงกระแทกมหาศาล (High-G) โดยใช้เซนเซอร์ MPU6050 เพื่อล็อกพิกัดสุดท้าย (Last Known Position) ก่อนถูกซากอาคารพังทับ
* 📡 **Deep-Penetration SOS:** ส่งสัญญาณขอความช่วยเหลือทะลวงซากคอนกรีตด้วยคลื่นความถี่ต่ำ LoRa 433 MHz ซึ่งมีอำนาจทะลุทะลวงสูง
* 🕸️ **Underground Mesh Relay:** ระบบทวนสัญญาณใยแมงมุม ให้กำไลใต้ซากตึกช่วยส่งต่อสัญญาณกันเองเพื่อเพิ่มระยะส่งไปยังภายนอก
* 🫀 **Survival Triage Logic:** คัดกรองผู้บาดเจ็บอัตโนมัติด้วยเซนเซอร์วัดชีพจร (MAX30102) และการเคาะรหัสขอความช่วยเหลือ (Tap-to-SOS)
* 🎯 **Last-Meter UWB Pinpointing:** เมื่อทีมกู้ภัยเข้าใกล้พื้นที่เป้าหมาย เครื่องค้นหาพกพาจะสลับจากโหมด LoRa ไปใช้ UWB (Two-Way Ranging) เพื่อบอกระยะทาง (เซนติเมตร) และทิศทางไปยังเป้าหมาย ช่วยให้ขุดเจาะได้ตรงจุด ไม่กระทบโครงสร้าง

---

## 2. สถาปัตยกรรมระบบ (System Architecture)

ระบบประกอบด้วยอุปกรณ์ 4 ประเภท ทำงานร่วมกันดังนี้:

1. **ภาวะปกติ (UWB Precision):**
   `Band-Node (กำไลข้อมือ)` <---> `ResQ-Pin (เสารับสัญญาณ Anchor)` ---> `Main-Node (ศูนย์สั่งการ)` เพื่อเก็บพิกัด
2. **ภาวะตึกถล่ม (LoRa Penetration & Mesh):**
   `Band-Node (ใต้ซาก)` ---> `Band-Node อื่นๆ (Mesh Relay)` ---> `ResQ-Pin (เสาภายนอก)` ---> `Main-Node`
3. **ภาวะค้นหาระยะใกล้ (UWB Last-Meter Pinpoint):**
   `ResQ-Node (เครื่องสแกนของทีมกู้ภัย)` <===Two-Way Ranging===> `Band-Node (เป้าหมาย)`
   - เครื่องของกู้ภัยจะทำหน้าที่ส่งสัญญาณสอบถาม (Initiator) กำไลข้อมือจะตอบกลับ (Responder) ทำให้รู้ทั้งระยะทางและทิศทางผ่านหน้าจอ OLED

การจัดสรรช่องสัญญาณ LoRa ทำงานผ่านระบบ **TDMA (Time Division Multiple Access)** รอบละ 10 วินาที โดยแบ่งเป็น 10 สล็อต เพื่อไม่ให้สัญญาณแต่ละอุปกรณ์ตีกัน (Slot 0=Beacon, Slot 1-2=Band, Slot 3-6=Pin, Slot 7=Main Cmd, Slot 8=ResQ-Node, Slot 9=Emergency)

---

## 3. รายละเอียดฮาร์ดแวร์และการเชื่อมต่อ (Hardware Details & Pinouts)

### 3.1. Smart Wristband Node (กำไลข้อมือ - Band-Node)
* **บอร์ดประมวลผล:** ESP32 DevKit V1
* **การสื่อสาร:** Ra-02 LoRa 433 MHz (SPI) + DW3000 UWB (SPI)
* **เซนเซอร์:** MPU6050 (3-Axis Accelerometer), MAX30102 (Pulse & SpO2) ใช้ I2C
* **อื่นๆ:** Buzzer, แบตเตอรี่ Li-Po 3.7V, วงจร Voltage Divider (ตัวต้านทาน 100k) วัดแบตเตอรี่
* **ขาเชื่อมต่อ (Pinout):**
  - LoRa: SCK=18, MISO=19, MOSI=23, SS=5, RST=14, DIO0=2
  - UWB: SS=4, IRQ=33, RST=32
  - I2C (Sensors): SDA=21, SCL=22, MPU_INT=27
  - Buzzer: 25, LED: 13, VBAT_ADC: 34

### 3.2. Gateway Pillar (เสารับสัญญาณ - ResQ-Pin)
* **บอร์ดประมวลผล:** ESP32 DevKit V1
* **ฟังก์ชัน:** ทำหน้าที่เป็น Anchor รับสัญญาณ LoRa เพื่อวัดความแรง (RSSI) ส่งให้ Main-Node นำไปคำนวณตำแหน่งวงกว้าง
* **ขาเชื่อมต่อ (Pinout):** 
  - LoRa: เหมือน Band-Node, VBAT_ADC: 36

### 3.3. Handheld Sweeper (เครื่องรับสัญญาณพกพา - ResQ-Node)
* **บอร์ดประมวลผล:** ESP32 DevKit V1
* **การสื่อสาร:** LoRa (สำหรับกวาดสัญญาณกว้าง), DW3000 UWB (สำหรับชี้เป้าระยะใกล้)
* **ส่วนแสดงผล:** จอ OLED 1.3" I2C (SH110X)
* **อื่นๆ:** มอเตอร์สั่น (Vibration), Buzzer, ปุ่มกดเปลี่ยนโหมด
* **ขาเชื่อมต่อ (Pinout):**
  - LoRa/UWB: เหมือน Band-Node (UWB IRQ=34, UWB RST=27)
  - OLED: SDA=21, SCL=22
  - Vibration=25, Buzzer=26, BTN_MODE=32, BTN_FOUND=33

### 3.4. Dispatch Hub (ศูนย์สั่งการ - Main-Node)
* **บอร์ดประมวลผล:** ESP32-S3 DevKitC (ใช้สำหรับต่อ USB-CDC Serial เข้าคอมพิวเตอร์โดยตรง)
* **ฟังก์ชัน:** ทำหน้าที่สร้าง TDMA Beacon ควบคุมวงจรคิวเวลา รับข้อมูลทั้งหมดส่งให้ Web App
* **ขาเชื่อมต่อ (Pinout):**
  - LoRa (HSPI/SPI2): SCK=12, MISO=13, MOSI=11, SS=10, RST=5, DIO0=4
  - LED Status (WS2812): 48, Buzzer=17

---

## 4. หลักการทำงานของซอฟต์แวร์ (Software Principles)

### 4.1. อัลกอริทึม UWB Last-Meter Search
ในพื้นที่ซากตึก คลื่นวิทยุจะมีปัญหาการสะท้อน (Multipath) ทำให้คาดเดาทิศทางยาก UWB ใช้พัลส์คลื่นที่สั้นมากๆ (< 2 ns) แบนด์วิดท์กว้าง ทะลุทะลวงระยะใกล้ได้ดี
* **Two-Way Ranging (TWR):** ResQ-Node ส่งคำถาม -> กำไลตอบ -> ResQ-Node สรุปเวลาเดินทางของคลื่น นำมาคำนวณระยะทางได้แม่นยำระดับเซนติเมตร
* **Phase Difference of Arrival (PDoA):** หากใช้เสาอากาศ UWB คู่ จะสามารถคำนวณความต่างเฟสของคลื่นที่มาถึง เพื่อบอกทิศทาง (องศา) บนเข็มทิศหน้าจอ OLED ได้

### 4.2. การประเมินสัญญาณชีพ (Triage Logic)
ค่าชีพจรที่ได้จาก MAX30102 จะถูกนำมาวิเคราะห์ตามหลักการแพทย์:
* ปรกติ: ชีพจร 55-110 BPM, SpO2 > 94%
* ฉุกเฉินวิกฤต: ชีพจร < 40 หรือ > 150 BPM, SpO2 < 85%
* หากเซนเซอร์ MPU6050 ตรวจพบแรงระดับ 4.0g ขึ้นไป จะถือว่ามีการตกหล่นหรือกระแทกรุนแรง และส่ง SOS ทันที

### 4.3. ระบบอัพเดทไร้สาย (OTA Update)
การอัพเดทเฟิร์มแวร์ทำผ่าน **WiFi + GitHub Releases** (ไม่ได้ทำผ่าน LoRa เนื่องจากแบนด์วิดท์ LoRa ต่ำมาก เพียงประมาณ 1-2 kbps)
* ระบบจะต่อ WiFi (ตามที่ตั้งใน `secrets.h`) และเช็คเวอร์ชัน .bin จาก GitHub ทุกๆ 6 ชั่วโมง 
* หากมีเวอร์ชันใหม่ จะโหลดไฟล์มา Flash และ Reboot เองอัตโนมัติ (รองรับบน Main-Node, ResQ-Pin, ResQ-Node)
* **หมายเหตุ:** Band-Node ปิดฟังก์ชันนี้ไว้เป็นค่าเริ่มต้นเพื่อประหยัดแบตเตอรี่

---

## 5. วิธีการติดตั้ง ทดสอบ และใช้งาน (Installation & Usage)

### 5.1. การลงโปรแกรม (Flashing Firmware)
โปรเจกต์ใช้ **PlatformIO** ใน VS Code 
1. เปิดโฟลเดอร์โปรเจกต์
2. ต่อสายบอร์ด
3. เลือก Environment ที่แถบด้านล่าง หรือรันคำสั่ง:
   - `pio run -e main_node -t upload` สำหรับศูนย์สั่งการ
   - `pio run -e band_node -t upload` สำหรับกำไล
   - `pio run -e resq_pin -t upload` สำหรับเสาสัญญาณ
   - `pio run -e resq_node -t upload` สำหรับเครื่องสแกนกู้ภัย

### 5.2. การรันระบบ Web Application (Dashboard)
1. ติดตั้ง Node.js
2. เปิด Terminal ในโฟลเดอร์ `app/`
3. รันคำสั่ง `npm install`
4. รันคำสั่ง `npm run dev`
5. เข้าเบราว์เซอร์ `http://localhost:3000` กดเชื่อมต่อ USB WebSerial เลือกพอร์ตของบอร์ด Main-Node

### 5.3. การทดสอบการทำงานเบื้องต้นในห้อง (Indoor Testing)
1. เปิด Main-Node และ Web App ให้พร้อมรับข้อมูล
2. เปิด Band-Node ทิ้งไว้ รอให้ไฟติดและส่งสัญญาณ ดูหน้า Web ว่าค่าชีพจร/แบตเตอรี่มาหรือไม่
3. ลองเคาะบอร์ด Band-Node แรงๆ ติดต่อกันเพื่อจำลอง Tap-to-SOS หรือเขย่าแรงๆ จำลอง Fall Detection หน้าจอ Web App ต้องมีหน้าต่าง SOS เด้งขึ้นมา
4. นำ ResQ-Pin ไปวางตามจุดต่างๆ ของห้องเพื่อดูค่าความแรงคลื่น (RSSI) ที่แสดงในหน้า Web App

---

## 6. รายการวัสดุอุปกรณ์ (Bill of Materials)

* ฮาร์ดแวร์ประมวลผล: ESP32 DevKit V1, ESP32-S3 DevKitC
* โมดูลสื่อสาร: Ra-02 LoRa 433 MHz, โมดูล UWB DW3000
* เซนเซอร์: GY-521 MPU6050 (ความเร่ง), MAX30102 (ชีพจร/ออกซิเจน)
* โมดูลแสดงผลและแจ้งเตือน: หน้าจอ OLED 1.3" I2C, Active Buzzer, มอเตอร์สั่น (Vibration Motor)
* อุปกรณ์เชื่อมต่อและพลังงาน: สาย Jumper Wire (M-M, M-F, F-F), สาย USB (Micro USB, Type-C), ตัวต้านทาน 100kΩ, แบตเตอรี่ Li-Po 3.7V / Power bank 10000mAh
* โครงสร้าง: เคสพลาสติก PLA จากเครื่องพิมพ์ 3D, แม่เหล็ก, น็อต, เชือก, ตัวหนีบ
* ซอฟต์แวร์: VS Code + PlatformIO, Node.js, Arduino IDE, FlashPrint, Application LINE (สำหรับรับแจ้งเตือน)
