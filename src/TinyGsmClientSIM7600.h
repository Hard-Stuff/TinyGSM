/**
 * @file       TinyGsmClientSIM7600.h
 * @author     Volodymyr Shymanskyy
 * @license    LGPL-3.0
 * @copyright  Copyright (c) 2016 Volodymyr Shymanskyy
 * @date       Nov 2016
 */

#ifndef SRC_TINYGSMCLIENTSIM7600_H_
#define SRC_TINYGSMCLIENTSIM7600_H_

// #define TINY_GSM_DEBUG Serial
// #define TINY_GSM_USE_HEX

#define TINY_GSM_MUX_COUNT 10
#define TINY_GSM_BUFFER_READ_AND_CHECK_SIZE

#include <utility>
#include "TinyGsmBattery.tpp"
#include "TinyGsmCalling.tpp"
#include "TinyGsmGPRS.tpp"
#include "TinyGsmGPS.tpp"
#include "TinyGsmGSMLocation.tpp"
#include "TinyGsmModem.tpp"
#include "TinyGsmSMS.tpp"
#include "TinyGsmTCP.tpp"
#include "TinyGsmTemperature.tpp"
#include "TinyGsmTime.tpp"
#include "TinyGsmNTP.tpp"


#define GSM_NL "\r\n"
static const char GSM_OK[] TINY_GSM_PROGMEM    = "OK" GSM_NL;
static const char GSM_ERROR[] TINY_GSM_PROGMEM = "ERROR" GSM_NL;
#if defined       TINY_GSM_DEBUG
static const char GSM_CME_ERROR[] TINY_GSM_PROGMEM = GSM_NL "+CME ERROR:";
static const char GSM_CMS_ERROR[] TINY_GSM_PROGMEM = GSM_NL "+CMS ERROR:";
#endif

enum RegStatus {
  REG_NO_RESULT    = -1,
  REG_UNREGISTERED = 0,
  REG_SEARCHING    = 2,
  REG_DENIED       = 3,
  REG_OK_HOME      = 1,
  REG_OK_ROAMING   = 5,
  REG_UNKNOWN      = 4,
};
enum class SSLVersion: int8_t {
  NO_SSL = -1,
  SSL3_0 = 0,
  TLS1_0 = 1,
  TLS1_1 = 2,
  TLS1_2 = 3,
  ALL_SSL = 4
};

class TinyGsmSim7600 : public TinyGsmModem<TinyGsmSim7600>,
                       public TinyGsmGPRS<TinyGsmSim7600>,
                       public TinyGsmTCP<TinyGsmSim7600, TINY_GSM_MUX_COUNT>,
                       public TinyGsmSMS<TinyGsmSim7600>,
                       public TinyGsmGSMLocation<TinyGsmSim7600>,
                       public TinyGsmGPS<TinyGsmSim7600>,
                       public TinyGsmTime<TinyGsmSim7600>,
                       public TinyGsmNTP<TinyGsmSim7600>,
                       public TinyGsmBattery<TinyGsmSim7600>,
                       public TinyGsmTemperature<TinyGsmSim7600>,
                       public TinyGsmCalling<TinyGsmSim7600> {
  friend class TinyGsmModem<TinyGsmSim7600>;
  friend class TinyGsmGPRS<TinyGsmSim7600>;
  friend class TinyGsmTCP<TinyGsmSim7600, TINY_GSM_MUX_COUNT>;
  friend class TinyGsmSMS<TinyGsmSim7600>;
  friend class TinyGsmGPS<TinyGsmSim7600>;
  friend class TinyGsmGSMLocation<TinyGsmSim7600>;
  friend class TinyGsmTime<TinyGsmSim7600>;
  friend class TinyGsmNTP<TinyGsmSim7600>;
  friend class TinyGsmBattery<TinyGsmSim7600>;
  friend class TinyGsmTemperature<TinyGsmSim7600>;
  friend class TinyGsmCalling<TinyGsmSim7600>;

  /*
   * Inner Client
   */
 public:
  class GsmClientSim7600 : public GsmClient {
    friend class TinyGsmSim7600;

   public:
    GsmClientSim7600() {}

    explicit GsmClientSim7600(TinyGsmSim7600& modem, uint8_t mux = 0) {
      init(&modem, mux);
    }

    bool init(TinyGsmSim7600* modem, uint8_t mux = 0) {
      this->at       = modem;
      sock_available = 0;
      prev_check     = 0;
      sock_connected = false;
      got_data       = false;

      if (mux < TINY_GSM_MUX_COUNT) {
        this->mux = mux;
      } else {
        this->mux = (mux % TINY_GSM_MUX_COUNT);
      }
      at->sockets[this->mux] = this;

      return true;
    }

   public:
    virtual int connect(const char* host, uint16_t port, int timeout_s) {
      stop();
      TINY_GSM_YIELD();
      rx.clear();
      sock_connected = at->modemConnect(host, port, mux, SSLVersion::NO_SSL,
                                        timeout_s);
      return sock_connected;
    }
    TINY_GSM_CLIENT_CONNECT_OVERRIDES

    virtual size_t modemGetAvailable(uint8_t mux) {
      if (!at->sockets[mux]) return 0;
      at->sendAT(GF("+CIPRXGET=4,"), mux);
      size_t result = 0;
      if (at->waitResponse(GF("+CIPRXGET:")) == 1) {
        at->streamSkipUntil(',');  // Skip mode 4
        at->streamSkipUntil(',');  // Skip mux
        result = at->streamGetIntBefore('\n');
        at->waitResponse();
      }
      // DBG("### Available:", result, "on", mux);
      if (!result) {
        at->sockets[mux]->sock_connected = at->modemGetConnected(mux);
      }
      return result;
    }

    virtual int16_t modemSend(const void* buff, size_t len, uint8_t mux) {
      at->sendAT(GF("+CIPSEND="), mux, ',', (uint16_t)len);
      if (at->waitResponse(GF(">")) != 1) { return 0; }
      at->stream.write(reinterpret_cast<const uint8_t*>(buff), len);
      at->stream.flush();
      if (at->waitResponse(GF(GSM_NL "+CIPSEND:")) != 1) { return 0; }
      at->streamSkipUntil(',');  // Skip mux
      at->streamSkipUntil(',');  // Skip requested bytes to send
      // TODO(?):  make sure requested and confirmed bytes match
      return at->streamGetIntBefore('\n');
    }

    virtual size_t modemRead(size_t size, uint8_t mux) {
    if (!at->sockets[mux]) return 0;
#ifdef TINY_GSM_USE_HEX
    sendAT(GF("+CIPRXGET=3,"), mux, ',', (uint16_t)size);
    if (waitResponse(GF("+CIPRXGET:")) != 1) { return 0; }
#else
    at->sendAT(GF("+CIPRXGET=2,"), mux, ',', (uint16_t)size);
    if (at->waitResponse(GF("+CIPRXGET:")) != 1) { return 0; }
#endif
    at->streamSkipUntil(',');  // Skip Rx mode 2/normal or 3/HEX
    at->streamSkipUntil(',');  // Skip mux/cid (connecion id)
    int16_t len_requested = at->streamGetIntBefore(',');
    //  ^^ Requested number of data bytes (1-1460 bytes)to be read
    int16_t len_confirmed = at->streamGetIntBefore('\n');
    // ^^ The data length which not read in the buffer
    for (int i = 0; i < len_requested; i++) {
      uint32_t startMillis = millis();
#ifdef TINY_GSM_USE_HEX
      while (stream.available() < 2 &&
             (millis() - startMillis < sockets[mux]->_timeout)) {
        TINY_GSM_YIELD();
      }
      char buf[4] = {
          0,
      };
      buf[0] = stream.read();
      buf[1] = stream.read();
      char c = strtol(buf, NULL, 16);
#else
      while (!at->stream.available() &&
             (millis() - startMillis < at->sockets[mux]->_timeout)) {
        TINY_GSM_YIELD();
      }
      char c = at->stream.read();
#endif
      at->sockets[mux]->rx.put(c);
    }
    // DBG("### READ:", len_requested, "from", mux);
    // sockets[mux]->sock_available = modemGetAvailable(mux);
    at->sockets[mux]->sock_available = len_confirmed;
    at->waitResponse();
    return len_requested;
  }

    virtual void stop(uint32_t maxWaitMs) {
      dumpModemBuffer(maxWaitMs);
      at->sendAT(GF("+CIPCLOSE="), mux);
      sock_connected = false;
      at->waitResponse();
    }
    void stop() override {
      stop(15000L);
    }

    /*
     * Extended API
     */

    String remoteIP() TINY_GSM_ATTR_NOT_IMPLEMENTED;
  };

  /*
   * Inner Secure Client
   */


  class GsmClientSecureSim7600 : public GsmClientSim7600 {
   public:
    GsmClientSecureSim7600() {
    }

    explicit GsmClientSecureSim7600(TinyGsmSim7600& modem, uint8_t mux = 0)
        : GsmClientSim7600(modem, mux) {}

   protected:
    String cacert;
    String clientcert;
    String clientkey;
    bool certValidation = true;
    SSLVersion sslVersion = SSLVersion::ALL_SSL;

   public:
    void setCACert(String certificateString) {
      cacert = std::move(certificateString);
    }

    void setCertificate(String certificateString) {
      clientcert = std::move(certificateString);
    }

    void setPrivateKey(String certificateString) {
      clientkey = std::move(certificateString);
    }

    void setCertValidation(bool validation = true) {
      certValidation = validation;
    }

    int16_t modemSend(const void* buff, size_t len, uint8_t mux) override {
      at->sendAT(GF("+CCHSEND="), mux, ',', (uint16_t)len);
      if (at->waitResponse(GF(">")) != 1) { return 0; }
      at->stream.write(reinterpret_cast<const uint8_t*>(buff), len);
      at->stream.flush();
      if (at->waitResponse() != 1) { return 0; }
      return len;
    }

    size_t modemRead(size_t size, uint8_t mux) override {
      if (!at->sockets[mux]) return 0;
#ifdef TINY_GSM_USE_HEX
// (todo) Don't know how to implement this!
      sendAT(GF("+CIPRXGET=3,"), mux, ',', (uint16_t)size);
      if (waitResponse(GF("+CIPRXGET:")) != 1) { return 0; }
#else
      at->sendAT(GF("+CCHRECV="), mux, ',', (uint16_t)size);
      if (at->waitResponse(GF("+CCHRECV:")) != 1) { return 0; }
#endif
      at->streamSkipUntil(',');  // Skip DATA
      at->streamSkipUntil(',');  // Skip mux/cid (connecion id)
      // int16_t len_requested = streamGetIntBefore(',');
      //  ^^ Requested number of data bytes (1-1460 bytes)to be read
      int16_t len_confirmed = at->streamGetIntBefore('\n');
      // ^^ The data length which not read in the buffer
      for (int i = 0; i < len_confirmed; i++) {
        uint32_t startMillis = millis();
#ifdef TINY_GSM_USE_HEX
// (todo) Don't know how to implement this!
        while (stream.available() < 2 &&
               (millis() - startMillis < sockets[mux]->_timeout)) {
          TINY_GSM_YIELD();
        }
        char buf[4] = {
            0,
        };
        buf[0] = stream.read();
        buf[1] = stream.read();
        char c = strtol(buf, NULL, 16);
#else
        while (!at->stream.available() &&
               (millis() - startMillis < at->sockets[mux]->_timeout)) {
          TINY_GSM_YIELD();
        }
        char c = at->stream.read();
#endif
        at->sockets[mux]->rx.put(c);
      }
      // DBG("### READ:", len_requested, "from", mux);
      // sockets[mux]->sock_available = len_confirmed;
      String await_response = "+CCHRECV: " + String(mux) + ",0";
      at->waitResponse(await_response.c_str());
      at->sockets[mux]->sock_available = (uint16_t)modemGetAvailable(mux);
      return len_confirmed;
    }

    size_t modemGetAvailable(uint8_t mux) override {
      size_t result = 0;
      if (!at->sockets[mux]) return 0;
      at->sendAT(GF("+CCHRECV?"));
      if (at->waitResponse(GF(GSM_NL "+CCHRECV: ")) != 1) {
        at->sendAT(GF("+CIPRXGET=4,"), mux);
        size_t result = 0;
        if (at->waitResponse(GF("+CIPRXGET:")) == 1) {
          at->streamSkipUntil(',');  // Skip mode 4
          at->streamSkipUntil(',');  // Skip mux
          result = at->streamGetIntBefore('\n');
          at->waitResponse();
        }
        // DBG("### Available:", result, "on", mux);
        if (!result) {
          at->sockets[mux]->sock_connected = at->modemGetConnected(mux);
        }
        return result;
      }
      int startMillis = millis();
      at->streamSkipUntil(',');  // Skip mode 4
      if (mux) {
        at->streamSkipUntil(',');  // Skip mode 4
        result = at->streamGetIntBefore('\n');
      } else {
        result = at->streamGetIntBefore(',');
      }
      return result;
    }

    int connect(const char* host, uint16_t port, int timeout_s) override {
      stop(15000L);
      TINY_GSM_YIELD();
      rx.clear();
      if (certValidation && cacert.isEmpty()) {return -1;}
      sock_connected = at->modemConnect(host, port, mux, sslVersion,
                                        timeout_s, cacert, clientcert,
                                        clientkey);
      return sock_connected;
    }

     void stop(uint32_t maxWaitMs) override {
      at->sendAT(GF("+CCHCLOSE="), mux);
      at->waitResponse(5000L);

      if (!cacert.isEmpty()) {
        at->sendAT(GF("+CCERTDELE=\"cacert"), static_cast<int>(mux), ".pem\"");
        at->waitResponse();
      }

      if (!clientcert.isEmpty()) {
        at->sendAT(GF("+CCERTDELE=\"clientcert"), static_cast<int>(mux),
                   ".pem\"");
        at->waitResponse();
      }

      if (!clientkey.isEmpty()) {
        at->sendAT(GF("+CCERTDELE=\"clientkey"), static_cast<int>(mux),
                   ".pem\"");
        at->waitResponse();
      }
      GsmClientSim7600::stop(maxWaitMs);
    }
    TINY_GSM_CLIENT_CONNECT_OVERRIDES
  };

  /*
   * Constructor
   */
 public:
  explicit TinyGsmSim7600(Stream& stream) : stream(stream) {
    memset(sockets, 0, sizeof(sockets));
  }

  /*
   * Basic functions
   */
 protected:
  bool initImpl(const char* pin = NULL) {
    DBG(GF("### TinyGSM Version:"), TINYGSM_VERSION);
    DBG(GF("### TinyGSM Compiled Module:  TinyGsmClientSIM7600"));

    if (!testAT()) { return false; }

    sendAT(GF("E0"));  // Echo Off
    if (waitResponse() != 1) { return false; }

#ifdef TINY_GSM_DEBUG
    sendAT(GF("+CMEE=2"));  // turn on verbose error codes
#else
    sendAT(GF("+CMEE=0"));  // turn off error codes
#endif
    waitResponse();

    DBG(GF("### Modem:"), getModemName());

    // Disable time and time zone URC's
    sendAT(GF("+CTZR=0"));
    if (waitResponse(10000L) != 1) { return false; }

    // Enable automatic time zome update
    sendAT(GF("+CTZU=1"));
    if (waitResponse(10000L) != 1) { return false; }

    SimStatus ret = getSimStatus();
    // if the sim isn't ready and a pin has been provided, try to unlock the sim
    if (ret != SIM_READY && pin != NULL && strlen(pin) > 0) {
      simUnlock(pin);
      return (getSimStatus() == SIM_READY);
    } else {
      // if the sim is ready, or it's locked but no pin has been provided,
      // return true
      return (ret == SIM_READY || ret == SIM_LOCKED);
    }
  }

  String getModemNameImpl() {
    String name = "SIMCom SIM7600";

    sendAT(GF("+CGMM"));
    String res2;
    if (waitResponse(1000L, res2) != 1) { return name; }
    res2.replace(GSM_NL "OK" GSM_NL, "");
    res2.replace("_", " ");
    res2.trim();

    name = res2;
    DBG("### Modem:", name);
    return name;
  }

  bool factoryDefaultImpl() {  // these commands aren't supported
    return false;
  }

  /*
   * Power functions
   */
 protected:
  bool restartImpl(const char* pin = NULL) {
    if (!testAT()) { return false; }
    sendAT(GF("+CRESET"));
    if (waitResponse(10000L) != 1) { return false; }
    delay(5000L);  // TODO(?):  Test this delay!
    return init(pin);
  }

  bool powerOffImpl() {
    sendAT(GF("+CPOF"));
    return waitResponse() == 1;
  }

  bool radioOffImpl() {
    if (!setPhoneFunctionality(4)) { return false; }
    delay(3000);
    return true;
  }

  bool sleepEnableImpl(bool enable = true) {
    sendAT(GF("+CSCLK="), enable);
    return waitResponse() == 1;
  }

  bool setPhoneFunctionalityImpl(uint8_t fun, bool reset = false) {
    sendAT(GF("+CFUN="), fun, reset ? ",1" : "");
    return waitResponse(10000L) == 1;
  }

  /*
   * Generic network functions
   */
 public:
  RegStatus getRegistrationStatus() {
    return (RegStatus)getRegistrationStatusXREG("CGREG");
  }

 protected:
  bool isNetworkConnectedImpl() {
    RegStatus s = getRegistrationStatus();
    return (s == REG_OK_HOME || s == REG_OK_ROAMING);
  }

 public:
  String getNetworkModes() {
    sendAT(GF("+CNMP=?"));
    if (waitResponse(GF(GSM_NL "+CNMP:")) != 1) { return ""; }
    String res = stream.readStringUntil('\n');
    waitResponse();
    return res;
  }

  int16_t getNetworkMode() {
    sendAT(GF("+CNMP?"));
    if (waitResponse(GF(GSM_NL "+CNMP:")) != 1) { return false; }
    int16_t mode = streamGetIntBefore('\n');
    waitResponse();
    return mode;
  }

  bool setNetworkMode(uint8_t mode) {
    sendAT(GF("+CNMP="), mode);
    return waitResponse() == 1;
  }

  String getLocalIPImpl() {
    sendAT(GF("+IPADDR"));  // Inquire Socket PDP address
    // sendAT(GF("+CGPADDR=1"));  // Show PDP address
    String res;
    if (waitResponse(10000L, res) != 1) { return ""; }
    res.replace(GSM_NL "OK" GSM_NL, "");
    res.replace(GSM_NL, "");
    res.trim();
    return res;
  }

  /*
   * GPRS functions
   */
 protected:
  bool gprsConnectImpl(const char* apn, const char* user = NULL,
                       const char* pwd = NULL) {
    gprsDisconnect();  // Make sure we're not connected first

    // Define the PDP context

    // The CGDCONT commands set up the "external" PDP context

    // Set the external authentication
    if (user && strlen(user) > 0) {
      sendAT(GF("+CGAUTH=1,0,\""), user, GF("\",\""), pwd, '"');
      waitResponse();
    }

    // Define external PDP context 1
    sendAT(GF("+CGDCONT=1,\"IP\",\""), apn, '"', ",\"0.0.0.0\",0,0");
    waitResponse();

    // Configure TCP parameters

    // Select TCP/IP application mode (command mode)
    sendAT(GF("+CIPMODE=0"));
    waitResponse();

    // Set Sending Mode - send without waiting for peer TCP ACK
    sendAT(GF("+CIPSENDMODE=0"));
    waitResponse();

    // Configure socket parameters
    // AT+CIPCCFG= <NmRetry>, <DelayTm>, <Ack>, <errMode>, <HeaderType>,
    //            <AsyncMode>, <TimeoutVal>
    // NmRetry = number of retransmission to be made for an IP packet
    //         = 10 (default)
    // DelayTm = number of milliseconds to delay before outputting received data
    //          = 0 (default)
    // Ack = sets whether reporting a string "Send ok" = 0 (don't report)
    // errMode = mode of reporting error result code = 0 (numberic values)
    // HeaderType = which data header of receiving data in multi-client mode
    //            = 1 (+RECEIVE,<link num>,<data length>)
    // AsyncMode = sets mode of executing commands
    //           = 0 (synchronous command executing)
    // TimeoutVal = minimum retransmission timeout in milliseconds = 75000
    sendAT(GF("+CIPCCFG=10,0,0,0,1,0,75000"));
    if (waitResponse() != 1) { return false; }

    // Configure timeouts for opening and closing sockets
    // AT+CIPTIMEOUT=<netopen_timeout> <cipopen_timeout>, <cipsend_timeout>
    sendAT(GF("+CIPTIMEOUT="), 75000, ',', 15000, ',', 15000);
    waitResponse();

    // Start the socket service

    // This activates and attaches to the external PDP context that is tied
    // to the embedded context for TCP/IP (ie AT+CGACT=1,1 and AT+CGATT=1)
    // Response may be an immediate "OK" followed later by "+NETOPEN: 0".
    // We to ignore any immediate response and wait for the
    // URC to show it's really connected.
    sendAT(GF("+NETOPEN"));
    if (waitResponse(75000L, GF(GSM_NL "+NETOPEN: 0")) != 1) { return false; }


    // Set the module to require manual reading of  rx buffer data.
    sendAT(GF("+CCHSET=0,1"));
    waitResponse(5000L);
    // Start the SSL client (doesn't mean we have to use it!)
    sendAT(GF("+CCHSTART"));  // Start the SSL client
    if (waitResponse(5000L) != 1) return false;

    return true;
  }

  bool gprsDisconnectImpl() {
    // Close all sockets and stop the socket service
    // Note: On the LTE models, this single command closes all sockets and the
    // service
    sendAT(GF("+NETCLOSE"));
    waitResponse(60000L, GF(GSM_NL "+NETCLOSE: 0"));
    // We assume this works, so we can do ssh disconnect too
    // stop the SSH client
    sendAT(GF("+CCHSTOP"));
    return (waitResponse(60000L, GF(GSM_NL "+CCHSTOP: 0")) != 1);
  }

  bool isGprsConnectedImpl() {
    sendAT(GF("+NETOPEN?"));
    // May return +NETOPEN: 1, 0.  We just confirm that the first number is 1
    if (waitResponse(GF(GSM_NL "+NETOPEN: 1")) != 1) { return false; }
    waitResponse();

    sendAT(GF("+IPADDR"));  // Inquire Socket PDP address
    // sendAT(GF("+CGPADDR=1")); // Show PDP address
    if (waitResponse() != 1) { return false; }

    return true;
  }

  /*
   * SIM card functions
   */
 protected:
  // Gets the CCID of a sim card via AT+CCID
  String getSimCCIDImpl() {
    sendAT(GF("+CICCID"));
    if (waitResponse(GF(GSM_NL "+ICCID:")) != 1) { return ""; }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  /*
   * Phone Call functions
   */
 protected:
  bool callHangupImpl() {
    sendAT(GF("+CHUP"));
    return waitResponse() == 1;
  }

  /*
   * Messaging functions
   */
 protected:
  // Follows all messaging functions per template

  /*
   * GSM Location functions
   */
 protected:
  // Can return a GSM-based location from CLBS as per the template

  /*
   * GPS/GNSS/GLONASS location functions
   */
 protected:
  // enable GPS
  bool enableGPSImpl() {
    sendAT(GF("+CGPS=1"));
    if (waitResponse() != 1) { return false; }
    return true;
  }

  bool disableGPSImpl() {
    sendAT(GF("+CGPS=0"));
    if (waitResponse() != 1) { return false; }
    return true;
  }

  // get the RAW GPS output
  String getGPSrawImpl() {
    sendAT(GF("+CGNSSINFO"));
    if (waitResponse(GF(GSM_NL "+CGNSSINFO:")) != 1) { return ""; }
    String res = stream.readStringUntil('\n');
    waitResponse();
    res.trim();
    return res;
  }

  // get GPS informations
  bool getGPSImpl(float* lat, float* lon, float* speed = 0, float* alt = 0,
                  int* vsat = 0, int* usat = 0, float* accuracy = 0,
                  int* year = 0, int* month = 0, int* day = 0, int* hour = 0,
                  int* minute = 0, int* second = 0) {
    sendAT(GF("+CGNSSINFO"));
    if (waitResponse(GF(GSM_NL "+CGNSSINFO:")) != 1) { return false; }

    uint8_t fixMode = streamGetIntBefore(',');  // mode 2=2D Fix or 3=3DFix
                                                // TODO(?) Can 1 be returned
    if (fixMode == 1 || fixMode == 2 || fixMode == 3) {
      // init variables
      float ilat = 0;
      char  north;
      float ilon = 0;
      char  east;
      float ispeed       = 0;
      float ialt         = 0;
      int   ivsat        = 0;
      int   iusat        = 0;
      float iaccuracy    = 0;
      int   iyear        = 0;
      int   imonth       = 0;
      int   iday         = 0;
      int   ihour        = 0;
      int   imin         = 0;
      float secondWithSS = 0;

      streamSkipUntil(',');               // GPS satellite valid numbers
      streamSkipUntil(',');               // GLONASS satellite valid numbers
      streamSkipUntil(',');               // BEIDOU satellite valid numbers
      ilat  = streamGetFloatBefore(',');  // Latitude in ddmm.mmmmmm
      north = stream.read();              // N/S Indicator, N=north or S=south
      streamSkipUntil(',');
      ilon = streamGetFloatBefore(',');  // Longitude in ddmm.mmmmmm
      east = stream.read();              // E/W Indicator, E=east or W=west
      streamSkipUntil(',');

      // Date. Output format is ddmmyy
      iday   = streamGetIntLength(2);    // Two digit day
      imonth = streamGetIntLength(2);    // Two digit month
      iyear  = streamGetIntBefore(',');  // Two digit year

      // UTC Time. Output format is hhmmss.s
      ihour = streamGetIntLength(2);  // Two digit hour
      imin  = streamGetIntLength(2);  // Two digit minute
      secondWithSS =
          streamGetFloatBefore(',');  // 4 digit second with subseconds

      ialt   = streamGetFloatBefore(',');  // MSL Altitude. Unit is meters
      ispeed = streamGetFloatBefore(',');  // Speed Over Ground. Unit is knots.
      streamSkipUntil(',');                // Course Over Ground. Degrees.
      streamSkipUntil(',');  // After set, will report GPS every x seconds
      iaccuracy = streamGetFloatBefore(',');  // Position Dilution Of Precision
      streamSkipUntil(',');   // Horizontal Dilution Of Precision
      streamSkipUntil(',');   // Vertical Dilution Of Precision
      streamSkipUntil('\n');  // TODO(?) is one more field reported??

      // Set pointers
      if (lat != NULL)
        *lat = (floor(ilat / 100) + fmod(ilat, 100.) / 60) *
            (north == 'N' ? 1 : -1);
      if (lon != NULL)
        *lon = (floor(ilon / 100) + fmod(ilon, 100.) / 60) *
            (east == 'E' ? 1 : -1);
      if (speed != NULL) *speed = ispeed;
      if (alt != NULL) *alt = ialt;
      if (vsat != NULL) *vsat = ivsat;
      if (usat != NULL) *usat = iusat;
      if (accuracy != NULL) *accuracy = iaccuracy;
      if (iyear < 2000) iyear += 2000;
      if (year != NULL) *year = iyear;
      if (month != NULL) *month = imonth;
      if (day != NULL) *day = iday;
      if (hour != NULL) *hour = ihour;
      if (minute != NULL) *minute = imin;
      if (second != NULL) *second = static_cast<int>(secondWithSS);

      waitResponse();
      return true;
    }

    waitResponse();
    return false;
  }


  /**
   *  CGNSSMODE: <gnss_mode>,<dpo_mode>
   *  This command is used to configure GPS, GLONASS, BEIDOU and QZSS support
   * mode. 0 : GLONASS 1 : BEIDOU 2 : GALILEO 3 : QZSS dpo_mode: 1 enable , 0
   * disable
   */
  String setGNSSModeImpl(uint8_t mode, bool dpo) {
    String res;
    sendAT(GF("+CGNSSMODE="), mode, ",", dpo);
    if (waitResponse(10000L, res) != 1) { return ""; }
    res.replace(GSM_NL, "");
    res.trim();
    return res;
  }

  uint8_t getGNSSModeImpl() {
    sendAT(GF("+CGNSSMODE?"));
    if (waitResponse(GF(GSM_NL "+CGNSSMODE:")) != 1) { return 0; }
    return stream.readStringUntil(',').toInt();
  }


  /*
   * Time functions
   */
 protected:
  // Can follow the standard CCLK function in the template

  /*
   * NTP server functions
   */
  // Can sync with server using CNTP as per template

  /*
   * Battery functions
   */
 protected:
  // returns volts, multiply by 1000 to get mV
  uint16_t getBattVoltageImpl() {
    sendAT(GF("+CBC"));
    if (waitResponse(GF(GSM_NL "+CBC:")) != 1) { return 0; }

    // get voltage in VOLTS
    float voltage = streamGetFloatBefore('\n');
    // Wait for final OK
    waitResponse();
    // Return millivolts
    uint16_t res = voltage * 1000;
    return res;
  }

  int8_t getBattPercentImpl() TINY_GSM_ATTR_NOT_AVAILABLE;

  uint8_t getBattChargeStateImpl() TINY_GSM_ATTR_NOT_AVAILABLE;

  bool getBattStatsImpl(uint8_t& chargeState, int8_t& percent,
                        uint16_t& milliVolts) {
    chargeState = 0;
    percent     = 0;
    milliVolts  = getBattVoltage();
    return true;
  }

  /*
   * Temperature functions
   */
 protected:
  // get temperature in degree celsius
  uint16_t getTemperatureImpl() {
    sendAT(GF("+CPMUTEMP"));
    if (waitResponse(GF(GSM_NL "+CPMUTEMP:")) != 1) { return 0; }
    // return temperature in C
    uint16_t res = streamGetIntBefore('\n');
    // Wait for final OK
    waitResponse();
    return res;
  }

  /*
   * Client related functions
   */
 protected:
  bool modemConnect(const char* host, uint16_t port, uint8_t mux,
                    SSLVersion sslVersion, int timeout_s = 15,
                    String cacert = "", String clientcert = "",
                    String clientkey = "") {
    if (sslVersion != SSLVersion::NO_SSL) {
      uint8_t authmode = 0;
      // List the certs available
      //   sendAT(GF("+CCERTLIST"));
      //   waitResponse(5000L);
      sendAT(GF("+CSSLCFG=\"sslversion\","), mux, ',',
             static_cast<int>(sslVersion));
      if (waitResponse(5000L) != 1) return false;

      if (!cacert.isEmpty()) {
        sendAT(GF("+CCERTDOWN=\"cacert"), static_cast<int>(mux), ".pem\",",
               cacert.length());

        if (waitResponse(GF(">"))) {
          stream.write(reinterpret_cast<const uint8_t*>(cacert.c_str()),
                       cacert.length());
          waitResponse(5000L);

        sendAT(GF("+CSSLCFG=\"cacert\","), mux, ",\"cacert",  // set the root CA
               static_cast<int>(mux), ".pem\"");
        if (waitResponse(5000L) != 1) return false;
        }
      }

      if (!clientcert.isEmpty()) {
        sendAT(GF("+CCERTDOWN=\"clientcert"),
               static_cast<int>(mux), ".pem\",", clientcert.length());

        if (waitResponse(GF(">"))) {
          stream.write(reinterpret_cast<const uint8_t*>(clientcert.c_str()),
                       clientcert.length());
          waitResponse(5000L);
        }

        sendAT(GF("+CSSLCFG=\"clientcert\","), mux, ",\"clientcert",
               static_cast<int>(mux), ".pem\"");  // set the client certificate
        if (waitResponse(5000L) != 1) return false;
      }

      if (!clientkey.isEmpty()) {
        sendAT(GF("+CCERTDOWN=\"clientkey"), static_cast<int>(mux), ".pem\",",
               clientkey.length());

        if (waitResponse(GF(">"))) {
          stream.write(reinterpret_cast<const uint8_t*>(clientkey.c_str()),
                       clientkey.length());
          waitResponse(5000L);
        }
        sendAT(GF("+CSSLCFG=\"clientkey\","), mux, ",\"clientkey",
               static_cast<int>(mux), ".pem\"");  // set the client key
        if (waitResponse(5000L) != 1) return false;
      }

      if (!cacert.isEmpty()) {
        authmode = 1;
        if (!clientcert.isEmpty() && !clientkey.isEmpty()) {
          authmode = 2;
        }
      } else if (!clientcert.isEmpty() && !clientkey.isEmpty()) {
        authmode = 3;
      }

      // set authenticationmode
      sendAT(GF("+CSSLCFG=\"authmode\","), mux, ',', authmode);
      if (waitResponse(5000L) != 1) return false;
    }

    // Make sure we'll be getting data manually on this connection
    sendAT(GF("+CIPRXGET=1"));
    if (waitResponse() != 1) { return false; }

    // Establish a connection in multi-socket mode
    uint32_t timeout_ms = ((uint32_t)timeout_s) * 1000;
    if (sslVersion != SSLVersion::NO_SSL) {
      // set the configured SSL context for the session
      // AT+CCHSSLCFG=<session_id>,<ssl_ctx_index>
      sendAT(GF("+CCHSSLCFG="), mux, ',', mux);
      if (waitResponse(5000L) != 1) return false;

      sendAT(GF("+CCHOPEN="), mux, ',', GF("\""), host, GF("\","), port);
      // The reply is OK followed by +CIPOPEN: <link_num>,<err> where <link_num>
      // is the mux number and <err> should be 0 if there's no error
      if (waitResponse(timeout_ms, GF(GSM_NL "+CCHOPEN:")) != 1) {
        return false;
      }

    } else {
      sendAT(GF("+CIPOPEN="), mux, ',', GF("\"TCP"), GF("\",\""),
             host, GF("\","), port);
      // The reply is OK followed by +CIPOPEN: <link_num>,<err> where <link_num>
      // is the mux number and <err> should be 0 if there's no error
      if (waitResponse(timeout_ms, GF(GSM_NL "+CIPOPEN:")) != 1) {
        return false;
      }
    }

    uint8_t opened_mux    = streamGetIntBefore(',');
    uint8_t opened_result = streamGetIntBefore('\n');
    if (opened_mux != mux || opened_result != 0) return false;
    return true;
  }


  // Secure specific overides
  int16_t modemSend(const void* buff, size_t len, uint8_t mux) {
    return sockets[mux]->modemSend(buff, len, mux);
  }
  size_t modemRead(size_t size, uint8_t mux) {
    return sockets[mux]->modemRead(size, mux);
  }
  size_t modemGetAvailable(uint8_t mux) {
    return sockets[mux]->modemGetAvailable(mux);
  }

  bool modemGetConnected(uint8_t mux) {
    // Read the status of all sockets at once
    sendAT(GF("+CIPOPEN?"));
    if (waitResponse(GF("+CIPOPEN:")) != 1) {
      // return false;  // TODO:  Why does this not read correctly?
    }
    for (int muxNo = 0; muxNo < TINY_GSM_MUX_COUNT; muxNo++) {
      // +CIPOPEN:<mux>,<State or blank...>
      String state = stream.readStringUntil('\n');
      if (state.indexOf(',') > 0) { sockets[muxNo]->sock_connected = true; }
      waitResponse(GF("+CIPOPEN:"));
    }
    waitResponse();  // Should be an OK at the end
    if (!sockets[mux]) return false;
    return sockets[mux]->sock_connected;
  }

  /*
   * Utilities
   */
 public:
  // TODO(vshymanskyy): Optimize this!
  int8_t waitResponse(uint32_t timeout_ms, String& data,
                      GsmConstStr r1 = GFP(GSM_OK),
                      GsmConstStr r2 = GFP(GSM_ERROR),
#if defined TINY_GSM_DEBUG
                      GsmConstStr r3 = GFP(GSM_CME_ERROR),
                      GsmConstStr r4 = GFP(GSM_CMS_ERROR),
#else
                      GsmConstStr r3 = NULL, GsmConstStr r4 = NULL,
#endif
                      GsmConstStr r5 = NULL) {
    /*String r1s(r1); r1s.trim();
    String r2s(r2); r2s.trim();
    String r3s(r3); r3s.trim();
    String r4s(r4); r4s.trim();
    String r5s(r5); r5s.trim();
    DBG("### ..:", r1s, ",", r2s, ",", r3s, ",", r4s, ",", r5s);*/
    data.reserve(64);
    uint8_t  index       = 0;
    uint32_t startMillis = millis();
    do {
      TINY_GSM_YIELD();
      while (stream.available() > 0) {
        TINY_GSM_YIELD();
        int8_t a = stream.read();
        if (a <= 0) continue;  // Skip 0x00 bytes, just in case
        data += static_cast<char>(a);
        if (r1 && data.endsWith(r1)) {
          index = 1;
          goto finish;
        } else if (r2 && data.endsWith(r2)) {
          index = 2;
          goto finish;
        } else if (r3 && data.endsWith(r3)) {
#if defined TINY_GSM_DEBUG
          if (r3 == GFP(GSM_CME_ERROR)) {
            streamSkipUntil('\n');  // Read out the error
          }
#endif
          index = 3;
          goto finish;
        } else if (r4 && data.endsWith(r4)) {
          index = 4;
          goto finish;
        } else if (r5 && data.endsWith(r5)) {
          index = 5;
          goto finish;
        } else if (data.endsWith(GF(GSM_NL "+CIPRXGET:"))) {
          int8_t mode = streamGetIntBefore(',');
          if (mode == 1) {
            int8_t mux = streamGetIntBefore('\n');
            if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
              sockets[mux]->got_data = true;
            }
            data = "";
            // DBG("### Got Data:", mux);
          } else {
            data += mode;
          }
        } else if (data.endsWith(GF(GSM_NL "+RECEIVE:"))) {
          int8_t  mux = streamGetIntBefore(',');
          int16_t len = streamGetIntBefore('\n');
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->got_data = true;
            if (len >= 0 && len <= 1024) { sockets[mux]->sock_available = len; }
          }
          data = "";
          // DBG("### Got Data:", len, "on", mux);
        } else if (data.endsWith(GF("+IPCLOSE:"))) {
          int8_t mux = streamGetIntBefore(',');
          streamSkipUntil('\n');  // Skip the reason code
          if (mux >= 0 && mux < TINY_GSM_MUX_COUNT && sockets[mux]) {
            sockets[mux]->sock_connected = false;
          }
          data = "";
          DBG("### Closed: ", mux);
        } else if (data.endsWith(GF("+CIPEVENT:"))) {
          // Need to close all open sockets and release the network library.
          // User will then need to reconnect.
          DBG("### Network error!");
          if (!isGprsConnected()) { gprsDisconnect(); }
          data = "";
        }
      }
    } while (millis() - startMillis < timeout_ms);
  finish:
    if (!index) {
      data.trim();
      if (data.length()) { DBG("### Unhandled:", data); }
      data = "";
    }
    // data.replace(GSM_NL, "/");
    // DBG('<', index, '>', data);
    return index;
  }

  int8_t waitResponse(uint32_t timeout_ms, GsmConstStr r1 = GFP(GSM_OK),
                      GsmConstStr r2 = GFP(GSM_ERROR),
#if defined TINY_GSM_DEBUG
                      GsmConstStr r3 = GFP(GSM_CME_ERROR),
                      GsmConstStr r4 = GFP(GSM_CMS_ERROR),
#else
                      GsmConstStr r3 = NULL, GsmConstStr r4 = NULL,
#endif
                      GsmConstStr r5 = NULL) {
    String data;
    return waitResponse(timeout_ms, data, r1, r2, r3, r4, r5);
  }

  int8_t waitResponse(GsmConstStr r1 = GFP(GSM_OK),
                      GsmConstStr r2 = GFP(GSM_ERROR),
#if defined TINY_GSM_DEBUG
                      GsmConstStr r3 = GFP(GSM_CME_ERROR),
                      GsmConstStr r4 = GFP(GSM_CMS_ERROR),
#else
                      GsmConstStr r3 = NULL, GsmConstStr r4 = NULL,
#endif
                      GsmConstStr r5 = NULL) {
    return waitResponse(1000, r1, r2, r3, r4, r5);
  }

 public:
  Stream& stream;

 protected:
  GsmClientSim7600* sockets[TINY_GSM_MUX_COUNT];
  const char*       gsmNL = GSM_NL;
};

#endif  // SRC_TINYGSMCLIENTSIM7600_H_
