// webserver - serves the embedded dark-theme config site and the REST API.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the HTTP server on port 80. Call after Wi-Fi is initialised.
void webserver_start(void);

#ifdef __cplusplus
}
#endif
