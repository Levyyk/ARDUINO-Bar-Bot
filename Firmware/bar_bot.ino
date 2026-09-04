#include <Arduino.h>
#include <U8g2lib.h>
#include <Servo.h>

// --- ПІНИ ЕКРАНА ТА ПЕРИФЕРІЇ ---
U8G2_SSD1306_128X64_NONAME_1_SW_I2C u8g2(U8G2_R0, /* clock=*/ 8, /* data=*/ 10, /* reset=*/ U8X8_PIN_NONE);

const int potPin = A0;      // Потенціометр 
const int btnPin = 2;       // Кнопка "Старт" 
const int pumpIn1 = 5;      // ШІМ-пін помпи (Налив)
const int pumpIn2 = 6;      // Напрямок помпи (Реверс)
const int servoPin = 9;     // Сервопривід
const int ledR = 12;        // Світлодіод Червоний
const int ledG = 11;        // Світлодіод Зелений
const int limitPins[3] = {4, 3, 7}; // Місце 1, 2, 3

// --- МЕХАНІКА ТА ПОРЦІЇ ---
Servo tapServo;
const int SERVO_ZERO = 157; // ОНОВЛЕНА ДОМАШНЯ ПОЗИЦІЯ
int currentServoAngle = SERVO_ZERO; // Пам'ять поточного кута для плавного руху

const int servoSpeed = 15; // Швидкість повороту крана (мс на 1 градус)

// ОНОВЛЕНІ КУТИ ДЛЯ СТАКАНІВ 1, 2, 3
const int servoPositions[3] = {109, 83, 45}; 

// Вирахуваний час: мілісекунд на 1 мл
const int timePerMl = 334; 

// --- ЛОГІЧНІ ЗМІННІ ---
bool wasEmpty = true;      
bool hasPoured = false;    

void setup() {
  pinMode(btnPin, INPUT_PULLUP); 
  for(int i = 0; i < 3; i++) {
    pinMode(limitPins[i], INPUT_PULLUP); 
  }

  pinMode(ledR, OUTPUT);
  pinMode(ledG, OUTPUT);
  
  pinMode(pumpIn1, OUTPUT);
  pinMode(pumpIn2, OUTPUT);
  digitalWrite(pumpIn1, LOW);
  digitalWrite(pumpIn2, LOW);

  tapServo.attach(servoPin);
  tapServo.write(SERVO_ZERO); 
  currentServoAngle = SERVO_ZERO;

  u8g2.begin();

  setLight(1, 0); 
  showMainScreen("Ready", 0);
}

void loop() {
  bool glass1 = (digitalRead(limitPins[0]) == LOW); 
  bool glass2 = (digitalRead(limitPins[1]) == LOW); 
  bool glass3 = (digitalRead(limitPins[2]) == LOW); 
  bool anyGlass = glass1 || glass2 || glass3;
  
  // Рахуємо, скільки всього стаканів стоїть
  int glassCount = (glass1 ? 1 : 0) + (glass2 ? 1 : 0) + (glass3 ? 1 : 0);

  int potValue = analogRead(potPin);
  int step = map(potValue, 0, 1023, 0, 8);
  int currentVolume = 10 + (step * 5);

  if (!anyGlass) {
    if (!wasEmpty) { 
      setLight(1, 0); 
      wasEmpty = true;
      hasPoured = false;
      showMainScreen("Place Glass", 0);
      delay(200); 
    } else {
      showMainScreen("Vol: ", currentVolume);
    }
  } 
  else {
    if (wasEmpty) {
      wasEmpty = false;
      showMainScreen("Vol: ", currentVolume);
    }

    if (!hasPoured) {
      if (digitalRead(btnPin) == LOW) {
        
        unsigned long pressStartTime = millis(); 
        bool isCleanMode = false;
        
        bool onlyFirstPlace = (glass1 && !glass2 && !glass3);

        while (digitalRead(btnPin) == LOW) {
          if (onlyFirstPlace && (millis() - pressStartTime >= 5000)) {
            isCleanMode = true;
            setLight(1, 1); 
            showInfoScreen("CLEANING MODE");
            break; 
          }
          delay(10); 
        }

        if (isCleanMode) {
          // === РЕЖИМ ПРОМИВКИ (Дозволений для 1 стакана) ===
          moveServoSmoothly(servoPositions[0]); 
          delay(500); 

          if (digitalRead(btnPin) == LOW && digitalRead(limitPins[0]) == LOW) {
            analogWrite(pumpIn1, 255); 
            digitalWrite(pumpIn2, LOW);
            
            while(digitalRead(btnPin) == LOW && digitalRead(limitPins[0]) == LOW) {
              delay(20); 
            }
            digitalWrite(pumpIn1, LOW); 
          }

          moveServoSmoothly(SERVO_ZERO); 
          delay(500);
          
          hasPoured = true;
          setLight(0, 1); 
          showInfoScreen("CLEANED!");
          delay(1500);
        } 
        else {
          // === РЕЖИМ НАЛИВУ ===
          
          if (glassCount == 1) {
            // ФІЧА: ЖАРТ ДЛЯ ОДНОГО СТАКАНА
            setLight(1, 0); // Червоне світло (відмова)
            showJokeScreen();
            delay(3500); // Показуємо жарт 3.5 секунди
            
            hasPoured = true; // Блокуємо налив, поки стакан не заберуть
          } 
          else {
            // КОНВЕЄР ДЛЯ 2 АБО 3 СТАКАНІВ
            if (digitalRead(limitPins[0]) == LOW) {
              pourDrink(currentVolume, servoPositions[0], limitPins[0]);
            }
            if (digitalRead(limitPins[1]) == LOW) {
              pourDrink(currentVolume, servoPositions[1], limitPins[1]);
            }
            if (digitalRead(limitPins[2]) == LOW) {
              pourDrink(currentVolume, servoPositions[2], limitPins[2]);
            }
            
            moveServoSmoothly(SERVO_ZERO);
            delay(500);

            hasPoured = true;
            setLight(0, 1); 
            showInfoScreen("COMPLETE!");
            delay(2000);
          }
        }
      }
    }
  }
  delay(50); 
}

// --- ФУНКЦІЯ ПЛАВНОГО РУХУ СЕРВОПРИВОДА ---
void moveServoSmoothly(int targetAngle) {
  if (currentServoAngle < targetAngle) {
    for (int pos = currentServoAngle; pos <= targetAngle; pos++) {
      tapServo.write(pos);
      delay(servoSpeed); 
    }
  } else {
    for (int pos = currentServoAngle; pos >= targetAngle; pos--) {
      tapServo.write(pos);
      delay(servoSpeed);
    }
  }
  currentServoAngle = targetAngle; 
}

// --- ФУНКЦІЯ НАЛИВУ (З ЕКСТРЕНИМ ГАЛЬМОМ) ---
void pourDrink(int ml, int angle, int activePin) {
  setLight(1, 1); 
  
  moveServoSmoothly(angle); 
  delay(400); 

  analogWrite(pumpIn1, 190); 
  digitalWrite(pumpIn2, LOW);
  
  unsigned long pourDelay = (unsigned long)ml * timePerMl; 
  unsigned long startTime = millis(); 
  
  while (millis() - startTime < pourDelay) {
    int progressMl = (millis() - startTime) / timePerMl;
    if (progressMl > ml) progressMl = ml;
    showPouringScreen(progressMl, ml);

    // АВАРІЙНЕ ЗНЯТТЯ СТАКАНА (Миттєве всмоктування)
    if (digitalRead(activePin) == HIGH) {
      digitalWrite(pumpIn1, LOW); 
      analogWrite(pumpIn2, 255);  
      delay(150);                 
      digitalWrite(pumpIn2, LOW); 

      showInfoScreen("GLASS REMOVED!");
      delay(1000);
      break; 
    }
    delay(50); 
  }

  digitalWrite(pumpIn1, LOW); 
  delay(200); 
}

// --- ФУНКЦІЇ ЕКРАНА ---

void showJokeScreen() {
  u8g2.firstPage();
  do {
    u8g2.drawRFrame(4, 4, 120, 56, 5); 
    u8g2.setFont(u8g2_font_helvB10_tr);
    
    String line1 = "NO DRINKING";
    String line2 = "ALONE! =)";
    String line3 = "Find a friend!";
    
    int w1 = u8g2.getStrWidth(line1.c_str());
    int w2 = u8g2.getStrWidth(line2.c_str());
    int w3 = u8g2.getStrWidth(line3.c_str());
    
    u8g2.setCursor((128 - w1) / 2, 20);
    u8g2.print(line1);
    
    u8g2.setCursor((128 - w2) / 2, 38);
    u8g2.print(line2);
    
    u8g2.setCursor((128 - w3) / 2, 56);
    u8g2.print(line3);
  } while (u8g2.nextPage());
}

void showMainScreen(String text, int value) {
  u8g2.firstPage();
  do {
    u8g2.setDrawColor(1);
    u8g2.drawBox(0, 0, 128, 18);
    u8g2.setDrawColor(0);
    u8g2.setFont(u8g2_font_helvB10_tr);
    
    int titleWidth = u8g2.getStrWidth("BAR BOT");
    u8g2.setCursor((128 - titleWidth) / 2, 14);
    u8g2.print("BAR BOT");
    
    u8g2.setDrawColor(1); 

    if (value > 0) {
      u8g2.setFont(u8g2_font_logisoso24_tr);
      String volStr = String(value);
      int volWidth = u8g2.getStrWidth(volStr.c_str());
      
      u8g2.setFont(u8g2_font_helvB12_tr);
      int mlWidth = u8g2.getStrWidth(" ml");
      
      int totalWidth = volWidth + mlWidth;
      int startX = (128 - totalWidth) / 2; 
      
      u8g2.setFont(u8g2_font_logisoso24_tr);
      u8g2.setCursor(startX, 56);
      u8g2.print(volStr);
      
      u8g2.setFont(u8g2_font_helvB12_tr);
      u8g2.setCursor(startX + volWidth, 56);
      u8g2.print(" ml");
    } else {
      u8g2.setFont(u8g2_font_helvB12_tr);
      int strWidth = u8g2.getStrWidth(text.c_str());
      u8g2.setCursor((128 - strWidth) / 2, 45);
      u8g2.print(text);
    }
  } while (u8g2.nextPage());
}

void showPouringScreen(int currentMl, int targetMl) {
  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_helvB10_tr);
    int titleWidth = u8g2.getStrWidth("POURING...");
    u8g2.setCursor((128 - titleWidth) / 2, 14);
    u8g2.print("POURING...");

    u8g2.setFont(u8g2_font_helvB14_tr);
    String progressStr = String(currentMl) + " / " + String(targetMl) + " ml";
    int progWidth = u8g2.getStrWidth(progressStr.c_str());
    u8g2.setCursor((128 - progWidth) / 2, 38);
    u8g2.print(progressStr);

    int barWidth = map(currentMl, 0, targetMl, 0, 116);
    u8g2.drawFrame(4, 48, 120, 12);     
    u8g2.drawBox(6, 50, barWidth, 8);   
  } while (u8g2.nextPage());
}

void showInfoScreen(String message) {
  u8g2.firstPage();
  do {
    u8g2.drawRFrame(4, 10, 120, 44, 5); 
    u8g2.setFont(u8g2_font_helvB10_tr);
    int strWidth = u8g2.getStrWidth(message.c_str());
    u8g2.setCursor((128 - strWidth) / 2, 38);
    u8g2.print(message);
  } while (u8g2.nextPage());
}

void setLight(bool r, bool g) {
  digitalWrite(ledR, r ? HIGH : LOW);
  digitalWrite(ledG, g ? HIGH : LOW);
}
