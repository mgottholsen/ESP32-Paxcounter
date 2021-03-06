// Basic Config
#include "globals.h"

  MessageBuffer_t SendBuffer;

// put data to send in RTos Queues used for transmit over channels Lora and SPI
void SendData(uint8_t port) {

  SendBuffer.MessageSize = payload.getSize();
  SendBuffer.MessagePort = PAYLOAD_ENCODER <= 2
                                 ? port
                                 : (PAYLOAD_ENCODER == 4 ? LPP2PORT : LPP1PORT);
  memcpy(SendBuffer.Message, payload.getBuffer(), payload.getSize());

  // enqueue message in LoRa send queue
#ifdef HAS_LORA
  if (xQueueSendToBack(LoraSendQueue, (void *)&SendBuffer, (TickType_t)0))
    ESP_LOGI(TAG, "%d bytes enqueued to send on LoRa", payload.getSize());
#endif

// enqueue message in SPI send queue
#ifdef HAS_SPI
  if (xQueueSendToBack(SPISendQueue, (void *)&SendBuffer, (TickType_t)0))
    ESP_LOGI(TAG, "%d bytes enqueued to send on SPI", payload.getSize());
#endif

  // clear counter if not in cumulative counter mode
  if ((port == COUNTERPORT) && (cfg.countermode != 1)) {
    reset_counters(); // clear macs container and reset all counters
    reset_salt();     // get new salt for salting hashes
    ESP_LOGI(TAG, "Counter cleared");
  }

} // SendData

// cyclic called function to prepare payload to send
void sendPayload() {
  if (SendCycleTimerIRQ) {
    portENTER_CRITICAL(&timerMux);
    SendCycleTimerIRQ = 0;
    portEXIT_CRITICAL(&timerMux);

    // append counter data to payload
    payload.reset();
    payload.addCount(macs_wifi, cfg.blescan ? macs_ble : 0);
    // append GPS data, if present

#ifdef HAS_GPS
    // show NMEA data in debug mode, useful for debugging GPS on board
    // connection
    ESP_LOGD(TAG, "GPS NMEA data: passed %d / failed: %d / with fix: %d",
             gps.passedChecksum(), gps.failedChecksum(),
             gps.sentencesWithFix());
    // log GPS position if we have a fix and gps data mode is enabled
    if ((cfg.gpsmode) && (gps.location.isValid())) {
      gps_read();
      payload.addGPS(gps_status);
      ESP_LOGD(TAG, "lat=%.6f | lon=%.6f | %u Sats | HDOP=%.1f | Altitude=%um",
               gps_status.latitude / (float)1e6,
               gps_status.longitude / (float)1e6, gps_status.satellites,
               gps_status.hdop / (float)100, gps_status.altitude);
    } else {
      ESP_LOGD(TAG, "No valid GPS position or GPS data mode disabled");
    }
#endif
    SendData(COUNTERPORT);
  }
} // sendpayload()

// interrupt handler used for payload send cycle timer
void IRAM_ATTR SendCycleIRQ() {
  portENTER_CRITICAL(&timerMux);
  SendCycleTimerIRQ++;
  portEXIT_CRITICAL(&timerMux);
}

// cyclic called function to eat data from RTos send queues and transmit it
void processSendBuffer() {

#ifdef HAS_LORA
  // Check if there is a pending TX/RX job running
  if ((LMIC.opmode & (OP_JOINING | OP_REJOIN | OP_TXDATA | OP_POLL)) != 0) {
    // LoRa Busy -> don't eat data from queue, since it cannot be sent
  } else {
    if (xQueueReceive(LoraSendQueue, &(SendBuffer), (TickType_t)10)) {
      // SendBuffer now holds the struct MessageBuffer with next payload from queue
      LMIC_setTxData2(SendBuffer.MessagePort, SendBuffer.Message, SendBuffer.MessageSize,
                      (cfg.countermode & 0x02));
      ESP_LOGI(TAG, "%d bytes sent to LORA", SendBuffer.MessageSize);
      sprintf(display_line7, "PACKET QUEUED");
    }
  }
#endif

#ifdef HAS_SPI
  if (xQueueReceive(SPISendQueue, &(SendBuffer), (TickType_t)10)) {
    ESP_LOGI(TAG, "%d bytes sent to SPI", SendBuffer.MessageSize);
  }
#endif

} // processSendBuffer

void flushQueues() {
#ifdef HAS_LORA
    xQueueReset(LoraSendQueue);
#endif
#ifdef HAS_SPI
    xQueueReset(SPISendQueue);
#endif
}