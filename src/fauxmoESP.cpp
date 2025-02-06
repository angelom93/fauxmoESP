/*

FAUXMO ESP

Copyright (C) 2016-2020 by Xose Pérez <xose dot perez at gmail dot com>

The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <Arduino.h>
#include "fauxmoESP.h"

// -----------------------------------------------------------------------------
// UDP
// -----------------------------------------------------------------------------

void fauxmoESP::_sendUDPResponse() {

	DEBUG_MSG_FAUXMO("[FAUXMO] Responding to M-SEARCH request\n");

	IPAddress ip = WiFi.localIP();
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();

    int needed_size = snprintf(NULL, 0, FAUXMO_UDP_RESPONSE_TEMPLATE, ip[0], ip[1], ip[2], ip[3], _tcp_port, mac.c_str(), mac.c_str()) + 1;
    char* response = (char*)malloc(needed_size);
    snprintf(response, needed_size, FAUXMO_UDP_RESPONSE_TEMPLATE, ip[0], ip[1], ip[2], ip[3], _tcp_port, mac.c_str(), mac.c_str());
    snprintf_P(
        response, sizeof(response),
        FAUXMO_UDP_RESPONSE_TEMPLATE,
        ip[0], ip[1], ip[2], ip[3],
		_tcp_port,
        mac.c_str(), mac.c_str()
    );

	#if DEBUG_FAUXMO_VERBOSE_UDP
    	DEBUG_MSG_FAUXMO("[FAUXMO] UDP response sent to %s:%d\n%s", _udp.remoteIP().toString().c_str(), _udp.remotePort(), response);
	#endif

    _udp.beginPacket(_udp.remoteIP(), _udp.remotePort());
	#if defined(ESP32)
	    _udp.printf(response);
	#else
	    _udp.write(response);
	#endif
    free(response);  // ✅ Avoids buffer overflow
    _udp.endPacket();

}

void fauxmoESP::_handleUDP() {

	int len = _udp.parsePacket();
    if (len > 0) {

		unsigned char data[len+1];
        _udp.read(data, len);
        data[len] = 0;

		#if DEBUG_FAUXMO_VERBOSE_UDP
			DEBUG_MSG_FAUXMO("[FAUXMO] UDP packet received\n%s", (const char *) data);
		#endif

        String request = (const char *) data;
        if (request.indexOf("M-SEARCH") >= 0) {
            if ((request.indexOf("ssdp:discover") > 0) || (request.indexOf("upnp:rootdevice") > 0) || (request.indexOf("device:basic:1") > 0)) {
                _sendUDPResponse();
            }
        }
    }

}


// -----------------------------------------------------------------------------
// TCP
// -----------------------------------------------------------------------------

void fauxmoESP::_sendTCPResponse(AsyncClient *client, const char * code, char * body, const char * mime) {

	char headers[strlen_P(FAUXMO_TCP_HEADERS) + 32];
	snprintf_P(
		headers, sizeof(headers),
		FAUXMO_TCP_HEADERS,
		code, mime, strlen(body)
	);

	#if DEBUG_FAUXMO_VERBOSE_TCP
		DEBUG_MSG_FAUXMO("[FAUXMO] Response:\n%s%s\n", headers, body);
	#endif

	client->write(headers);
	client->write(body);

}

String fauxmoESP::_deviceJson(unsigned char id, bool all = true) {
    if (id >= _devices.size()) return "{}";

    fauxmoesp_device_t device = _devices[id];

    DEBUG_MSG_FAUXMO("[FAUXMO] Sending device info for \"%s\", uniqueID = \"%s\", complete_info = %s\n",
                     device.name, device.uniqueid, all ? "true" : "false");

    // Step 1: Calculate the required buffer size dynamically
    int needed_size;
    if (all) {
        needed_size = snprintf(NULL, 0, FAUXMO_DEVICE_JSON_TEMPLATE,
                               device.name, device.uniqueid,
                               device.state ? "true" : "false",
                               device.value, device.hue, device.sat, device.colorTemp,
                               (device.mode == 'h' ? "hs" : device.mode == 'c' ? "ct" : "xy")) + 1;
    } else {
        needed_size = snprintf(NULL, 0, FAUXMO_DEVICE_JSON_TEMPLATE_SHORT,
                               device.name, device.uniqueid) + 1;
    }

    // Step 2: Allocate buffer dynamically
    char* buffer = (char*)malloc(needed_size);
    if (!buffer) return "{}";  // Return empty JSON if memory allocation fails

    // Step 3: Fill the buffer with formatted data
    if (all) {
        snprintf(buffer, needed_size, FAUXMO_DEVICE_JSON_TEMPLATE,
                 device.name, device.uniqueid,
                 device.state ? "true" : "false",
                 device.value, device.hue, device.sat, device.colorTemp,
                 (device.mode == 'h' ? "hs" : device.mode == 'c' ? "ct" : "xy"));
    } else {
        snprintf(buffer, needed_size, FAUXMO_DEVICE_JSON_TEMPLATE_SHORT,
                 device.name, device.uniqueid);
    }

    // Step 4: Convert to `String` and free memory
    String jsonString = String(buffer);
    free(buffer);

    return jsonString;
}


String fauxmoESP::_byte2hex(uint8_t zahl)
{
  String hstring = String(zahl, HEX);
  if (zahl < 16)
  {
    hstring = "0" + hstring;
  }

  return hstring;
}

String fauxmoESP::_makeMD5(String text)
{
  unsigned char bbuf[16];
  String hash = "";
  MD5Builder md5;
  md5.begin();
  md5.add(text);
  md5.calculate();
  
  md5.getBytes(bbuf);
  for (uint8_t i = 0; i < 16; i++)
  {
    hash += _byte2hex(bbuf[i]);
  }

  return hash;
}

bool fauxmoESP::_onTCPDescription(AsyncClient *client, String url, String body) {

	(void) url;
	(void) body;

	DEBUG_MSG_FAUXMO("[FAUXMO] Handling /description.xml request\n");

	IPAddress ip = WiFi.localIP();
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toLowerCase();

	char response[strlen_P(FAUXMO_DESCRIPTION_TEMPLATE) + 64];
    snprintf_P(
        response, sizeof(response),
        FAUXMO_DESCRIPTION_TEMPLATE,
        ip[0], ip[1], ip[2], ip[3], _tcp_port,
        ip[0], ip[1], ip[2], ip[3], _tcp_port,
        mac.c_str(), mac.c_str()
    );

	_sendTCPResponse(client, "200 OK", response, "text/xml");

	return true;

}


bool fauxmoESP::_onTCPList(AsyncClient *client, String url, String body) {
	DEBUG_MSG_FAUXMO("[FAUXMO] Handling list request for: url=%s, body=%s\n", url.c_str(), body.c_str());

	// Get the index
	int pos = url.indexOf("lights");
	if (-1 == pos) return false;

	// Get the id
	unsigned char id = url.substring(pos+7).toInt();


	// This will hold the response string	
	String response;

	// Client is requesting all devices
	if (0 == id) {
		DEBUG_MSG_FAUXMO("[FAUXMO] Sending all devices\n");
		response += "{";
		for (unsigned char i=0; i< _devices.size(); i++) {
			if (i>0) response += ",";
			response += "\"" + String(i+1) + "\":" + _deviceJson(i, false);	// send short template
		}
		response += "}";

	// Client is requesting a single device
	} else {
		DEBUG_MSG_FAUXMO("[FAUXMO] Sending device %d\n", id);
		response = _deviceJson(id-1);
	}

	_sendTCPResponse(client, "200 OK", (char *) response.c_str(), "application/json");
	DEBUG_MSG_FAUXMO("[FAUXMO] Response: %s\n", response.c_str());
	
	return true;

}

// byte* fauxmoESP::_hs2rgb(uint16_t hue, uint8_t sat) {
// 	byte *rgb = new byte[3]{0, 0, 0};

// 	float h = ((float)hue)/65535.0;
//     float s = ((float)sat)/255.0;

//     byte i = floor(h*6);
//     float f = h * 6-i;
//     float p = 255 * (1-s);
//     float q = 255 * (1-f*s);
//     float t = 255 * (1-(1-f)*s);
//     switch (i%6) {
//       case 0: rgb[0]=255,rgb[1]=t,rgb[2]=p;break;
//       case 1: rgb[0]=q,rgb[1]=255,rgb[2]=p;break;
//       case 2: rgb[0]=p,rgb[1]=255,rgb[2]=t;break;
//       case 3: rgb[0]=p,rgb[1]=q,rgb[2]=255;break;
//       case 4: rgb[0]=t,rgb[1]=p,rgb[2]=255;break;
//       case 5: rgb[0]=255,rgb[1]=p,rgb[2]=q;
//     }
// 	return rgb;
// }


// uint16_t* fauxmoESP::_rgb2hs(byte r, byte g, byte b) {
//     // ✅ Allocate an array for Hue (16-bit) and Saturation (8-bit)
//     uint16_t* hs = new uint16_t[2]{0, 0}; 

//     // Normalize RGB values to [0, 1]
//     float rf = r / 255.0;
//     float gf = g / 255.0;
//     float bf = b / 255.0;

//     // Get max and min values of RGB
//     float maxVal = max(rf, max(gf, bf));
//     float minVal = min(rf, min(gf, bf));
//     float delta = maxVal - minVal;

//     // ✅ Calculate Hue correctly
//     float h = 0;
//     if (delta > 0) {
//         if (maxVal == rf) {
//             h = fmod(((gf - bf) / delta), 6.0);
//             if (h < 0) h += 6.0; // Ensure positive hue
//         } else if (maxVal == gf) {
//             h = ((bf - rf) / delta) + 2.0;
//         } else {
//             h = ((rf - gf) / delta) + 4.0;
//         }
//         h *= 60.0; // Convert to degrees
//     }

//     // ✅ Calculate Saturation correctly
//     float s = (maxVal > 0) ? (delta / maxVal) : 0;

//     // ✅ Scale results correctly
//     hs[0] = (uint16_t)((h / 360.0) * 65534.0); // Scale hue to 0-65534
//     hs[1] = (uint8_t)(s * 254.0);  // Scale saturation to 0-254

//     // ✅ Ensure values are within valid range
//     if (hs[1] == 0) hs[1] = 1;
//     if (hs[1] == 255) hs[1] = 254;
//     if (hs[0] == 0) hs[0] = 1;
//     if (hs[0] == 65535) hs[0] = 65534;

//     return hs; // ✅ Correctly return uint16_t* (16-bit array)
// }

bool fauxmoESP::_onTCPControl(AsyncClient *client, String url, String body) {
    // Debug: Print the full body of the incoming message
    DEBUG_MSG_FAUXMO("[FAUXMO] Received Body:\n%s\n", body.c_str());

    // "devicetype" request
    if (body.indexOf("devicetype") > 0) {
        DEBUG_MSG_FAUXMO("[FAUXMO] Handling devicetype request\n");
        _sendTCPResponse(client, "200 OK", (char *)"[{\"success\":{\"username\": \"2WLEDHardQrI3WHYTHoMcXHgEspsM8ZZRpSKtBQr\"}}]", "application/json");
        return true;
    }

    // "state" request
    if ((url.indexOf("state") > 0) && (body.length() > 0)) {
        // Get the index
        int pos = url.indexOf("lights");
        if (pos == -1) return false;

        DEBUG_MSG_FAUXMO("[FAUXMO] Handling state request\n");

        // Get the device ID
        unsigned char id = url.substring(pos + 7).toInt();
        if (id > 0) {
            --id;

            // send response fast to prevent timeouts
            char buf[50];
            snprintf_P(buf, sizeof(buf), PSTR("[{\"success\":{\"/lights/%u/state/\": true}}]"), id+1, _devices[id].state ? "true" : "false");
            _sendTCPResponse(client, "200 OK", buf, "application/json");

            if (body.indexOf("\"xy\"") > 0) {
                _devices[id].mode = 'x'; // XY mode
            } else if (body.indexOf("\"ct\"") > 0) {
                _devices[id].mode = 'c'; // Color temperature mode
            } else {
                _devices[id].mode = 'h'; // Hue/Saturation mode
            }

            if (body.indexOf("false") > 0) {
                _devices[id].state = false;
            } else if (body.indexOf("true") > 0) {
                _devices[id].state = true;
            }

            // Brightness
            if ((pos = body.indexOf("bri")) > 0) {
                unsigned char value = body.substring(pos + 5).toInt();
                _devices[id].state = (value > 0);
				if (value == 255) value = 254;
                _devices[id].value = value;
            }

            // Hue and Saturation
            if ((pos = body.indexOf("hue")) > 0) {
                _devices[id].state = true;
                unsigned int pos_comma = body.indexOf(",", pos);
                uint16_t hue = body.substring(pos + 5, pos_comma).toInt();
                pos = body.indexOf("sat", pos_comma);
                unsigned char sat = body.substring(pos + 5).toInt();
                _devices[id].hue = hue;
                _devices[id].sat = sat;
                // reset color temperature
                _devices[id].colorTemp = 0;
            }

            // Color Temperature
            if ((pos = body.indexOf("ct")) > 0) {
                _devices[id].state = true;
                uint16_t ct = body.substring(pos + 4).toInt();
                _devices[id].colorTemp = ct;
                // reset hue and saturation
                _devices[id].hue = 0;
                _devices[id].sat = 0;
            }


            // Callbacks
            if (_setStateCallback) {
                _setStateCallback(id, _devices[id].name, _devices[id].state, _devices[id].value);
            }
            if (_setStateWithColorCallback) {
                _setStateWithColorCallback(id, _devices[id].name, _devices[id].state, _devices[id].value, _devices[id].hue, _devices[id].sat);
            }
            if (_setStateWithColorTempCallback) {
                _setStateWithColorTempCallback(
                    id,
                    _devices[id].name,
                    _devices[id].state,
                    _devices[id].value,
                    _devices[id].hue,
                    _devices[id].sat,
                    _devices[id].colorTemp
                );
            }

            return true;
        }
    }

    return false;
}

bool fauxmoESP::_onTCPRequest(AsyncClient *client, bool isGet, String url, String body) {
    if (!_enabled) return false;

	#if DEBUG_FAUXMO_VERBOSE_TCP
		DEBUG_MSG_FAUXMO("[FAUXMO] isGet: %s\n", isGet ? "true" : "false");
		DEBUG_MSG_FAUXMO("[FAUXMO] URL: %s\n", url.c_str());
		if (!isGet) DEBUG_MSG_FAUXMO("[FAUXMO] Body:\n%s\n", body.c_str());
	#endif

	if (url.equals("/description.xml")) {
        return _onTCPDescription(client, url, body);
    }

	if (url.startsWith("/api")) {
		if (isGet) {
			return _onTCPList(client, url, body);
		} else {
       		return _onTCPControl(client, url, body);
		}
	}

	return false;

}

bool fauxmoESP::_onTCPData(AsyncClient *client, void *data, size_t len) {

    if (!_enabled) return false;

	char * p = (char *) data;
	p[len] = 0;

	#if DEBUG_FAUXMO_VERBOSE_TCP
		DEBUG_MSG_FAUXMO("[FAUXMO] TCP request\n%s\n", p);
	#endif

	// Method is the first word of the request
	char * method = p;

	while (*p != ' ') p++;
	*p = 0;
	p++;
	
	// Split word and flag start of url
	char * url = p;

	// Find next space
	while (*p != ' ') p++;
	*p = 0;
	p++;

	// Find double line feed
	unsigned char c = 0;
	while ((*p != 0) && (c < 2)) {
		if (*p != '\r') {
			c = (*p == '\n') ? c + 1 : 0;
		}
		p++;
	}
	char * body = p;

	bool isGet = (strncmp(method, "GET", 3) == 0);

	return _onTCPRequest(client, isGet, url, body);

}

void fauxmoESP::_onTCPClient(AsyncClient *client) {

    if (_enabled) {
        for (unsigned char i = 0; i < FAUXMO_TCP_MAX_CLIENTS; i++) {
            if (!_tcpClients[i] || !_tcpClients[i]->connected()) {

                // Clean up any previous disconnected client
                if (_tcpClients[i]) {
                    delete _tcpClients[i];  // Proper cleanup
                }

                _tcpClients[i] = client;  // Assign new client

                client->onAck([i](void *s, AsyncClient *c, size_t len, uint32_t time) {
                    // No changes needed here
                }, 0);

                client->onData([this, i](void *s, AsyncClient *c, void *data, size_t len) {
                    _onTCPData(c, data, len);
                }, 0);

                client->onDisconnect([this, i](void *s, AsyncClient *c) {
                    if (_tcpClients[i]) {
                        delete _tcpClients[i];  // Proper cleanup
                        _tcpClients[i] = nullptr;
                    }
                    DEBUG_MSG_FAUXMO("[FAUXMO] Client #%d disconnected\n", i);
                }, 0);

                client->onError([i](void *s, AsyncClient *c, int8_t error) {
                    DEBUG_MSG_FAUXMO("[FAUXMO] Error %s (%d) on client #%d\n", c->errorToString(error), error, i);
                }, 0);

                client->onTimeout([i](void *s, AsyncClient *c, uint32_t time) {
                    DEBUG_MSG_FAUXMO("[FAUXMO] Timeout on client #%d at %i\n", i, time);
                    c->close();
                }, 0);

                client->setRxTimeout(FAUXMO_RX_TIMEOUT);

                DEBUG_MSG_FAUXMO("[FAUXMO] Client #%d connected\n", i);
                return;
            }
        }

        DEBUG_MSG_FAUXMO("[FAUXMO] Rejecting - Too many connections\n");

        // If too many clients, close and delete this one immediately
        client->close();
        delete client;

    } else {
        DEBUG_MSG_FAUXMO("[FAUXMO] Rejecting - Disabled\n");

        // Cleanup client if Fauxmo is disabled
        client->close();
        delete client;
    }
}


// -----------------------------------------------------------------------------
// Devices
// -----------------------------------------------------------------------------

fauxmoESP::~fauxmoESP() {
  	
	// Free the name for each device
	for (auto& device : _devices) {
		free(device.name);
  	}
  	
	// Delete devices  
	_devices.clear();

}

void fauxmoESP::setDeviceUniqueId(unsigned char id, const char *uniqueid)
{
    strncpy(_devices[id].uniqueid, uniqueid, FAUXMO_DEVICE_UNIQUE_ID_LENGTH);
}

unsigned char fauxmoESP::addDevice(const char * device_name) {

    fauxmoesp_device_t device;
    unsigned int device_id = _devices.size();

    // init properties
    device.name = strdup(device_name);
  	device.state = true;
	  device.value = 100;
      device.hue = 1;
      device.sat = 1;
	  device.colorTemp = 50;
	  device.mode = 'h'; // possible bvalues 'hs', 'xy', 'ct'

    // create the uniqueid
    String mac = WiFi.macAddress();

    snprintf(device.uniqueid, FAUXMO_DEVICE_UNIQUE_ID_LENGTH, "%02X:%s:%s", device_id, mac.c_str(), "00:00");


    // Attach
    _devices.push_back(device);

    DEBUG_MSG_FAUXMO("[FAUXMO] Device '%s' added as #%d\n", device_name, device_id);

    return device_id;

}

int fauxmoESP::getDeviceId(const char * device_name) {
    for (unsigned int id=0; id < _devices.size(); id++) {
        if (strcmp(_devices[id].name, device_name) == 0) {
            return id;
        }
    }
    return -1;
}

bool fauxmoESP::renameDevice(unsigned char id, const char * device_name) {
    if (id < _devices.size()) {
        free(_devices[id].name);
        _devices[id].name = strdup(device_name);
        DEBUG_MSG_FAUXMO("[FAUXMO] Device #%d renamed to '%s'\n", id, device_name);
        return true;
    }
    return false;
}

bool fauxmoESP::renameDevice(const char * old_device_name, const char * new_device_name) {
	int id = getDeviceId(old_device_name);
	if (id < 0) return false;
	return renameDevice(id, new_device_name);
}

bool fauxmoESP::removeDevice(unsigned char id) {
    if (id < _devices.size()) {
        free(_devices[id].name);
		_devices.erase(_devices.begin()+id);
        DEBUG_MSG_FAUXMO("[FAUXMO] Device #%d removed\n", id);
        return true;
    }
    return false;
}

bool fauxmoESP::removeDevice(const char * device_name) {
	int id = getDeviceId(device_name);
	if (id < 0) return false;
	return removeDevice(id);
}

char * fauxmoESP::getDeviceName(unsigned char id, char * device_name, size_t len) {
    if ((id < _devices.size()) && (device_name != NULL)) {
        strncpy(device_name, _devices[id].name, len);
    }
    return device_name;
}

bool fauxmoESP::setState(unsigned char id, bool state, unsigned char value) {
    if (id < _devices.size()) {
		_devices[id].state = state;
		_devices[id].value = value;
		return true;
	}
	return false;
}

bool fauxmoESP::setState(const char * device_name, bool state, unsigned char value) {
	return setState(getDeviceId(device_name), state, value);
}

bool fauxmoESP::setState(unsigned char id, bool state, unsigned char value, uint16_t hue, unsigned char sat) {
    if (id < _devices.size()) {
        _devices[id].state = state;
        _devices[id].value = value;
        _devices[id].hue = hue;
        _devices[id].sat = sat;
        return true;
    }
}

bool fauxmoESP::setState(const char * device_name, bool state, unsigned char value, uint16_t hue, unsigned char sat) {
    return setState(getDeviceId(device_name), state, value, hue, sat);
}

bool fauxmoESP::setState(unsigned char id, bool state, unsigned char value, uint16_t hue, unsigned char sat, uint16_t colorTemp) {
    if (id >= _devices.size()) return false;

    // Update the device state
    _devices[id].state = state;
    if (value == 255) value = 254;
    _devices[id].value = value;
    _devices[id].hue = hue;
    _devices[id].sat = sat;
    _devices[id].colorTemp = colorTemp; // Set color temperature

    return true;
}

bool fauxmoESP::setState(const char* device_name, bool state, unsigned char value, uint16_t hue, unsigned char sat, uint16_t colorTemp) {
    return setState(getDeviceId(device_name), state, value, hue, sat, colorTemp);
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

bool fauxmoESP::process(AsyncClient *client, bool isGet, String url, String body) {
	return _onTCPRequest(client, isGet, url, body);
}

void fauxmoESP::handle() {
    if (_enabled) _handleUDP();
}

void fauxmoESP::enable(bool enable) {

	if (enable == _enabled) return;
    _enabled = enable;
	if (_enabled) {
		DEBUG_MSG_FAUXMO("[FAUXMO] Enabled\n");
	} else {
		DEBUG_MSG_FAUXMO("[FAUXMO] Disabled\n");
	}

    if (_enabled) {

		// Start TCP server if internal
		if (_internal) {
			if (NULL == _server) {
				_server = new AsyncServer(_tcp_port);
				_server->onClient([this](void *s, AsyncClient* c) {
					_onTCPClient(c);
				}, 0);
			}
			_server->begin();
		}

		// UDP setup
		#ifdef ESP32
            _udp.beginMulticast(FAUXMO_UDP_MULTICAST_IP, FAUXMO_UDP_MULTICAST_PORT);
        #else
            _udp.beginMulticast(WiFi.localIP(), FAUXMO_UDP_MULTICAST_IP, FAUXMO_UDP_MULTICAST_PORT);
        #endif
        DEBUG_MSG_FAUXMO("[FAUXMO] UDP server started\n");

	}

}
