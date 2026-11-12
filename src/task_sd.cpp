#include <SD.h>
#include "config.h"
#include "shared.h"
#include "tasks.h"
#define CSV_SEP ","

static char   buf[SD_BUF_SIZE + 128];
static size_t len = 0;

static uint32_t t0_us  = 0;
static bool     haveT0 = false;

static File openNewFile() {
  char name[24];
  for (int i = 1; i < 10000; i++) {
    snprintf(name, sizeof(name), "/log_%04d.csv", i);
    if (!SD.exists(name)) {
      File f = SD.open(name, FILE_WRITE);
           if (f) {
        const uint8_t bom[3] = {0xEF, 0xBB, 0xBF};   // để Excel đọc đúng UTF-8
        f.write(bom, 3);
        f.print("time(s)" CSV_SEP "Ax(m/s²)" CSV_SEP "Ay(m/s²)" CSV_SEP "Az(m/s²)"
                CSV_SEP "Rx(°/s)" CSV_SEP "Ry(°/s)" CSV_SEP "Rz(°/s)\n");
        haveT0 = false;                      // thời gian tính lại từ 0 cho file mới
        Serial.printf("Mo file: %s\n", name);
      }
      return f;
    }
  }
  return File();
}

static void appendSample(File &f, const imu_sample_t &s) {
  if (!haveT0) { t0_us = s.t_us; haveT0 = true; }

  double t  = (double)(uint32_t)(s.t_us - t0_us) / 1e6;
  const double ka = GRAVITY_MS2 / ACC_LSB_PER_G;
  const double kg = 1.0 / GYRO_LSB_PER_DPS;

  len += snprintf(buf + len, sizeof(buf) - len,
                  "%.3f" CSV_SEP "%.3f" CSV_SEP "%.3f" CSV_SEP "%.3f"
                  CSV_SEP "%.3f" CSV_SEP "%.3f" CSV_SEP "%.3f\n",
                  t,
                  s.ax * ka, s.ay * ka, s.az * ka,
                  s.gx * kg, s.gy * kg, s.gz * kg);
  gWritten++;
  if (len >= SD_BUF_SIZE) {
    f.write((uint8_t *)buf, len);
    len = 0;
  }
}

void TaskSD(void *) {
  File f;
  uint32_t lastFlush = millis();
  imu_sample_t s;

  for (;;) {
    if (!gRecording) {
      if (f) {
        while (xQueueReceive(qImu, &s, 0) == pdTRUE) appendSample(f, s);
        if (len) { f.write((uint8_t *)buf, len); len = 0; }
        f.close();
        Serial.println("Da dong file, co the rut the");
      }
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (!f) {
      f = openNewFile();
      if (!f) {
        Serial.println("Khong mo duoc file");
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }
      len = 0;
      lastFlush = millis();
    }

    if (xQueueReceive(qImu, &s, pdMS_TO_TICKS(100)) == pdTRUE) {
      appendSample(f, s);
    }

    if (millis() - lastFlush > SD_FLUSH_MS) {
      if (len) { f.write((uint8_t *)buf, len); len = 0; }
      f.flush();
      lastFlush = millis();
    }
  }
}