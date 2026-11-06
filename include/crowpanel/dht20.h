#pragma once

#include <stdint.h>

#ifndef ENABLE_DHT20
#define ENABLE_DHT20 1
#endif

/** Initialize the DHT20 sensor. */
bool dht20_init();
/** Read integer temperature and humidity values. */
bool dht20_read(int & temperature, int & humidity);
