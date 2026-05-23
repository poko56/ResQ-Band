<div align="center">

# 🛟 ResQ-Band
**The IoT Wearable Emergency Localization and Survival Triage System** *ระบบกำไลข้อมืออัจฉริยะชี้เป้าและคัดกรองผู้รอดชีวิตใต้ซากอาคารถล่มสำหรับงาน US&R*

</div>

---

## 📌 Introduction (บทนำ)

จากปัญหาอุบัติเหตุอาคารและสิ่งก่อสร้างถล่ม **"เวลาทองคำ (Golden Hour)"** คือสิ่งสำคัญที่สุดในการช่วยชีวิต ทว่าโครงสร้างคอนกรีตและเหล็กเส้นมักดักจับและสะท้อนคลื่นวิทยุทั่วไป ทำให้ระบบระบุตำแหน่งมาตรฐานไม่สามารถทำงานได้ 

**ResQ-Band** คือโปรเจกต์ Open-Source ฮาร์ดแวร์และซอฟต์แวร์ที่ผสานเทคโนโลยี **Ultra-Wideband (UWB)** สำหรับการระบุตำแหน่งความแม่นยำสูงในภาวะปกติ และ **LoRa (433 MHz)** สำหรับการส่งสัญญาณทะลุทะลวงซากปรักหักพังในภาวะฉุกเฉิน ทำงานร่วมกับระบบประเมินสัญญาณชีพ เพื่อช่วยให้ทีมกู้ภัย (Urban Search & Rescue) สามารถชี้เป้าหมายและช่วยเหลือผู้รอดชีวิตได้อย่างแม่นยำที่สุด

---

## ✨ Features (จุดเด่นของระบบ)

* 📍 **Precision Pre-Collapse Tracking:** ใช้คลื่น UWB ประเมินพิกัดแบบ Real-time ระดับเซนติเมตรขณะอาคารยังไม่ถล่ม (Smart Zone Attendance)
* 💥 **The Fall Trigger:** อัลกอริทึมตรวจจับสภาวะตกอิสระ (Free-fall) หรือแรงกระแทกมหาศาล (High-G) เพื่อล็อกพิกัดสุดท้าย (Last Known Position) ก่อนซากอาคารพังทับ
* 📡 **Deep-Penetration SOS:** ส่งสัญญาณขอความช่วยเหลือทะลวงซากคอนกรีตด้วยคลื่นความถี่ต่ำ LoRa 433 MHz
* 🕸️ **Underground Mesh Relay:** ระบบทวนสัญญาณใยแมงมุม (ESP-NOW) ให้กำไลใต้ซากตึกช่วยส่งต่อสัญญาณกันเองเพื่อเพิ่มระยะส่ง
* 🫀 **Survival Triage Logic:** คัดกรองผู้บาดเจ็บอัตโนมัติด้วยเซนเซอร์วัดชีพจรและการเคาะรหัสขอความช่วยเหลือ (Tap-to-SOS)

---

## 🏗️ System Architecture (สถาปัตยกรรมระบบ)

```text
[ ภาวะปกติ: UWB Precision ]
กำไลข้อมือ (UWB Tag) <---> เสารับสัญญาณ (UWB Anchors) ---> Firebase (พิกัด Real-time)

[ ภาวะตึกถล่ม: LoRa Penetration & Mesh ]
กำไลข้อมือ (LoRa) ---> กำไลข้างเคียง (ESP-NOW Relay) ---> เสารับสัญญาณภายนอก (LoRa Gateway)
      |
      v
เครื่องค้นหาพกพาของทีมกู้ภัย (LoRa Yagi Antenna) ---> วิเคราะห์และชี้เป้าหมายภาคพื้นดิน
```

---

## 🛠️ Hardware Components (อุปกรณ์ที่ใช้พัฒนา)

โปรเจกต์นี้แบ่งฮาร์ดแวร์ออกเป็น 3 ส่วนหลัก (จำนวนขึ้นอยู่กับการสเกลระบบหน้างาน):

### 1. Smart Wristband Node (กำไลข้อมือ)
* **Microcontroller:** `ESP32-C3` (บอร์ดขนาดเล็ก ประหยัดพลังงาน)
* **Penetration Comm:** `Ra-02 LoRa 433 MHz` + `เสาสปริง 433 MHz`
* **Precision Comm:** `โมดูล UWB (เช่น DW3000)` สำหรับระบุพิกัดความแม่นยำสูง
* **Sensors:** `MPU6050` (3-Axis Accelerometer) และ `GY-MAX30102` (Heart Rate & SpO2)
* **Misc:** แบตเตอรี่ LiPo 3.7V, โมดูลชาร์จ TP4056, เรกูเลเตอร์ AMS1117, และ Active Buzzer 3.3V
* **Enclosure:** เคสพิมพ์ 3 มิติ วัสดุ TPU ยืดหยุ่นและกันกระแทก

### 2. Gateway Pillar (เสารับสัญญาณรอบอาคาร)
* **Microcontroller:** `ESP32 Devkit`
* **Modules:** `Ra-02 LoRa 433 MHz` และ `UWB Anchor Module`
* **Antenna:** `เสายางทรงแท่ง 433 MHz (Omni-directional, แนะนำ 7 dBi)` พร้อมสายแปลง IPEX to SMA
* **Power:** ระบบไฟสำรองอิสระ (Battery 18650 Pack)

### 3. Handheld Sweeper (เครื่องรับสัญญาณพกพาสำหรับกู้ภัย)
* **Microcontroller:** `ESP32 Devkit` + `Ra-02 LoRa 433 MHz`
* **Display:** `OLED Display 0.96" (I2C)` สำหรับแสดงค่า RSSI แบบ Real-time
* **Antenna:** `เสาอากาศ Yagi 433 MHz` (แบบกำหนดทิศทาง) สำหรับแกะรอยและชี้เป้าหมาย

---

## 💻 Software & Services

* **Embedded C/C++:** พัฒนาผ่าน VS Code (PlatformIO) หรือ Arduino IDE
* **Database & Dashboard:** Firebase Realtime Database สำหรับรับส่งข้อมูล และ Web Application สำหรับแสดงผลหน้าจอ Command Center
* **3D CAD:** SolidWorks หรือ Fusion 360 สำหรับออกแบบเคสอุปกรณ์

---

## 🤝 Contributing
โปรเจกต์นี้ยินดีรับการปรับปรุงและพัฒนาต่อยอดจากนักพัฒนาทุกท่าน หากพบปัญหาหรือมีข้อเสนอแนะ สามารถเปิด Issue หรือส่ง Pull Request ได้เลยครับ

## 📄 License
Distributed under the MIT License. See `LICENSE` for more information.
