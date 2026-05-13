/**************************************************************************
 *
 * Filename: ps5000aWrap.c
 *
 * Description:
 *  The source code in this release is for use with Pico products when 
 *	interfaced with Microsoft Excel VBA, National Instruments LabVIEW and 
 *	MathWorks MATLAB or any third-party programming language or application 
 *	that is unable to support C-style callback functions or structures.
 *
 *	You may modify, copy and distribute the source code for the purpose of 
 *	developing programs to collect data using Pico products to add new 
 *	functionality. If you modify the standard functions provided, we cannot 
 *	guarantee that they will work with the above-listed programming 
 *	languages or third-party products.
 *
 *  Please refer to the PicoScope 5000 Series (A API) Programmer's Guide
 *  for descriptions of the underlying functions where stated.
 *
 *  This wrapper supports up to PS5000A_WRAP_MAX_PICO_DEVICES PicoScope
 *  5000A devices streaming simultaneously. Each device's state (ready,
 *  buffers, channel counts, trigger flags, etc.) is held in a per-device
 *  WRAP_DEVICE_INFO slot keyed by the driver handle. The caller MUST call
 *  initWrapDevice(handle) once after ps5000aOpenUnit for each scope, and
 *  releaseWrapDevice(handle) after ps5000aCloseUnit.
 *
 * Copyright (C) 2013-2018 Pico Technology Ltd. See LICENSE file for terms.
 *
 **************************************************************************/

#include <string.h>

#include "ps5000aWrap.h"

 /////////////////////////////////
 //
 //	Per-device state table
 //
 /////////////////////////////////

static WRAP_DEVICE_INFO _deviceInfo[PS5000A_WRAP_MAX_PICO_DEVICES];
static uint16_t          _deviceCount = 0;

// Find the slot for the given handle. Returns NULL if not found.
static WRAP_DEVICE_INFO * findDevice(int16_t handle)
{
	int i;

	if (handle <= 0)
	{
		return NULL;
	}

	for (i = 0; i < PS5000A_WRAP_MAX_PICO_DEVICES; i++)
	{
		if (_deviceInfo[i].handle == handle)
		{
			return &_deviceInfo[i];
		}
	}

	return NULL;
}

/****************************************************************************
* initWrapDevice
*
* Allocate a slot of per-device state for the given handle. Must be called
* once per device, after ps5000aOpenUnit and before any other wrapper call
* that uses that handle.
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 initWrapDevice(int16_t handle)
{
	int i;

	if (handle <= 0)
	{
		return PICO_INVALID_HANDLE;
	}

	// Already initialised? Reset its state but keep the slot.
	for (i = 0; i < PS5000A_WRAP_MAX_PICO_DEVICES; i++)
	{
		if (_deviceInfo[i].handle == handle)
		{
			memset(&_deviceInfo[i], 0, sizeof(WRAP_DEVICE_INFO));
			_deviceInfo[i].handle = handle;
			return PICO_OK;
		}
	}

	// Find a free slot.
	for (i = 0; i < PS5000A_WRAP_MAX_PICO_DEVICES; i++)
	{
		if (_deviceInfo[i].handle == 0)
		{
			memset(&_deviceInfo[i], 0, sizeof(WRAP_DEVICE_INFO));
			_deviceInfo[i].handle = handle;
			_deviceCount++;
			return PICO_OK;
		}
	}

	return PICO_MAX_UNITS_OPENED;
}

/****************************************************************************
* releaseWrapDevice
*
* Releases the per-device wrapper state for the given handle. Should be called
* after ps5000aCloseUnit.
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 releaseWrapDevice(int16_t handle)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	memset(dev, 0, sizeof(WRAP_DEVICE_INFO));
	if (_deviceCount > 0)
	{
		_deviceCount--;
	}
	return PICO_OK;
}

/****************************************************************************
* getWrapDeviceCount
****************************************************************************/
extern uint16_t PREF0 PREF1 getWrapDeviceCount(void)
{
	return _deviceCount;
}

/////////////////////////////////
//
//	Function definitions
//
/////////////////////////////////

/****************************************************************************
* Streaming Callback
*
* See ps5000aStreamingReady (callback)
*
****************************************************************************/
void PREF1 StreamingCallback(
	int16_t handle,
	int32_t noOfSamples,
	uint32_t startIndex,
	int16_t overflow,
	uint32_t triggerAt,
	int16_t triggered,
	int16_t autoStop,
	void * pParameter)
{
	int16_t channel = 0;
	int16_t digitalPort = 0;
	WRAP_DEVICE_INFO * dev = NULL;
	WRAP_BUFFER_INFO * wrapBufferInfo = NULL;

	if (pParameter != NULL)
	{
		dev = (WRAP_DEVICE_INFO *) pParameter;
	}
	else
	{
		dev = findDevice(handle);
	}

	if (dev == NULL)
	{
		return;
	}

	wrapBufferInfo = &dev->wrapBufferInfo;

	dev->numSamples = noOfSamples;
	dev->autoStop = autoStop;
	dev->startIndex = startIndex;

	dev->triggered = triggered;
	dev->triggeredAt = triggerAt;

	dev->overflow = overflow;

	if (noOfSamples)
	{
		// Analogue channels
		for (channel = (int16_t) PS5000A_CHANNEL_A; channel < dev->channelCount; channel++)
		{
			if (dev->enabledChannels[channel])
			{
				if (wrapBufferInfo->appBuffers && wrapBufferInfo->driverBuffers)
				{
					// Max buffers
					if (wrapBufferInfo->appBuffers[channel * 2]  && wrapBufferInfo->driverBuffers[channel * 2])
					{
						memcpy_s(&wrapBufferInfo->appBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t),
							&wrapBufferInfo->driverBuffers[channel * 2][startIndex], noOfSamples * sizeof(int16_t));
					}

					// Min buffers
					if (wrapBufferInfo->appBuffers[channel * 2 + 1] && wrapBufferInfo->driverBuffers[channel * 2 + 1])
					{
						memcpy_s(&wrapBufferInfo->appBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t),
							&wrapBufferInfo->driverBuffers[channel * 2 + 1][startIndex], noOfSamples * sizeof(int16_t));
					}
				}
			}
		}

		// Digital channels
		if (dev->digitalPortCount > 0)
		{
			// Use index 0 to indicate Digital Port 0
			for (digitalPort = (int16_t)PS5000A_WRAP_DIGITAL_PORT0; digitalPort < dev->digitalPortCount; digitalPort++)
			{
				if (dev->enabledDigitalPorts[digitalPort])
				{
					// Copy data...
					if (wrapBufferInfo->appDigiBuffers && wrapBufferInfo->driverDigiBuffers)
					{
						// Max digital buffers
						if (wrapBufferInfo->appDigiBuffers[digitalPort * 2] && wrapBufferInfo->driverDigiBuffers[digitalPort * 2])
						{
							memcpy_s(&wrapBufferInfo->appDigiBuffers[digitalPort * 2][startIndex], noOfSamples * sizeof(int16_t),
								&wrapBufferInfo->driverDigiBuffers[digitalPort * 2][startIndex], noOfSamples * sizeof(int16_t));
						}

						// Min digital buffers
						if (wrapBufferInfo->appDigiBuffers[digitalPort * 2 + 1] && wrapBufferInfo->driverDigiBuffers[digitalPort * 2 + 1])
						{
							memcpy_s(&wrapBufferInfo->appDigiBuffers[digitalPort * 2 + 1][startIndex], noOfSamples * sizeof(int16_t),
								&wrapBufferInfo->driverDigiBuffers[digitalPort * 2 + 1][startIndex], noOfSamples * sizeof(int16_t));
						}
					}
				}
			}
		}
	}

	dev->ready = 1;
}

/****************************************************************************
* BlockCallback
*
* See ps5000aBlockReady (callback)
*
****************************************************************************/
void PREF1 BlockCallback(int16_t handle, PICO_STATUS status, void * pParameter)
{
	WRAP_DEVICE_INFO * dev = NULL;

	if (pParameter != NULL)
	{
		dev = (WRAP_DEVICE_INFO *) pParameter;
	}
	else
	{
		dev = findDevice(handle);
	}

	if (dev != NULL)
	{
		dev->ready = 1;
	}
}

/****************************************************************************
* RunBlock
*
* This function starts collecting data in block mode without the requirement 
* for specifying callback functions. Use the IsReady function in conjunction 
* to poll the driver once this function has been called.
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 RunBlock(int16_t handle, int32_t preTriggerSamples, int32_t postTriggerSamples,
            uint32_t timebase, uint32_t segmentIndex)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	dev->ready = 0;
	dev->numSamples = preTriggerSamples + postTriggerSamples;

	return ps5000aRunBlock(handle, preTriggerSamples, postTriggerSamples, timebase,
		NULL, segmentIndex, BlockCallback, dev);
}

/****************************************************************************
* GetStreamingLatestValues
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 GetStreamingLatestValues(int16_t handle)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	dev->ready = 0;
	dev->numSamples = 0;
	dev->autoStop = 0;

	return ps5000aGetStreamingLatestValues(handle, StreamingCallback, dev);
}

/****************************************************************************
* AvailableData
****************************************************************************/
extern uint32_t PREF0 PREF1 AvailableData(int16_t handle, uint32_t *startIndex)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev != NULL && dev->ready)
	{
		*startIndex = dev->startIndex;
		return dev->numSamples;
	}

	return 0;
}

/****************************************************************************
* AutoStopped
****************************************************************************/
extern int16_t PREF0 PREF1 AutoStopped(int16_t handle)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev != NULL && dev->ready)
	{
		return dev->autoStop;
	}

	return 0;
}

/****************************************************************************
* IsReady
****************************************************************************/
extern int16_t PREF0 PREF1 IsReady(int16_t handle)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return 0;
	}

	return dev->ready;
}

/****************************************************************************
* IsTriggerReady
****************************************************************************/
extern int16_t PREF0 PREF1 IsTriggerReady(int16_t handle, uint32_t *triggeredAt)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return 0;
	}

	if (dev->triggered)
	{
		*triggeredAt = dev->triggeredAt;
	}

	return dev->triggered;
}

/****************************************************************************
* ClearTriggerReady
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 ClearTriggerReady(int16_t handle)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	dev->triggeredAt = 0;
	dev->triggered = FALSE;
	return PICO_OK;
}

/****************************************************************************
* SetTriggerConditions  (deprecated - use SetTriggerConditionsV2)
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetTriggerConditions(int16_t handle, int32_t *conditionsArray, int16_t nConditions)
{
	PICO_STATUS status;
	int16_t i = 0;
	int16_t j = 0;

	PS5000A_TRIGGER_CONDITIONS *conditions = (PS5000A_TRIGGER_CONDITIONS *) calloc (nConditions, sizeof(PS5000A_TRIGGER_CONDITIONS));

	for (i = 0; i < nConditions; i++)
	{
		conditions[i].channelA				= (PS5000A_TRIGGER_STATE) conditionsArray[j];
		conditions[i].channelB				= (PS5000A_TRIGGER_STATE) conditionsArray[j + 1];
		conditions[i].channelC				= (PS5000A_TRIGGER_STATE) conditionsArray[j + 2];
		conditions[i].channelD				= (PS5000A_TRIGGER_STATE) conditionsArray[j + 3];
		conditions[i].external				= (PS5000A_TRIGGER_STATE) conditionsArray[j + 4];
		conditions[i].aux					= (PS5000A_TRIGGER_STATE) conditionsArray[j + 5];
		conditions[i].pulseWidthQualifier	= (PS5000A_TRIGGER_STATE) conditionsArray[j + 6];

		j = j + 7;
	}
	status = ps5000aSetTriggerChannelConditions(handle, conditions, nConditions);
	free (conditions);

	return status;
}

/****************************************************************************
* SetTriggerProperties  (deprecated - use SetTriggerPropertiesV2)
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetTriggerProperties(
	int16_t handle, 
	int32_t *propertiesArray, 
	int16_t nProperties, 
	int32_t autoTrig)
{
	PS5000A_TRIGGER_CHANNEL_PROPERTIES *channelProperties = (PS5000A_TRIGGER_CHANNEL_PROPERTIES *) calloc(nProperties, sizeof(PS5000A_TRIGGER_CHANNEL_PROPERTIES));
	int16_t i;
	int16_t j=0;
	int16_t auxEnable = 0;
	PICO_STATUS status;
	
	for (i = 0; i < nProperties; i++)
	{
		channelProperties[i].thresholdUpper				= propertiesArray[j];
		channelProperties[i].thresholdUpperHysteresis	= propertiesArray[j + 1];
		channelProperties[i].thresholdLower				= propertiesArray[j + 2];
		channelProperties[i].thresholdLowerHysteresis	= propertiesArray[j + 3];
		channelProperties[i].channel					= (PS5000A_CHANNEL) propertiesArray[j + 4];
		channelProperties[i].thresholdMode				= (PS5000A_THRESHOLD_MODE) propertiesArray[j + 5];

		j = j + 6;
	}
	status = ps5000aSetTriggerChannelProperties(handle, channelProperties, nProperties, auxEnable, autoTrig);
	free(channelProperties);
	
	return status;
}

/****************************************************************************
* SetPulseWidthQualifier  (deprecated)
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetPulseWidthQualifier(
	int16_t handle,
	int32_t *pwqConditionsArray,
	int16_t nConditions,
	int32_t direction,
	uint32_t lower,
	uint32_t upper,
	int32_t type)
{
	// Allocate memory
	PS5000A_PWQ_CONDITIONS *pwqConditions = (PS5000A_PWQ_CONDITIONS *) calloc(nConditions, sizeof(PS5000A_PWQ_CONDITIONS));

	int16_t i;
	int16_t j = 0;

	PICO_STATUS status;

	for (i = 0; i < nConditions; i++)
	{
		pwqConditions[i].channelA = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j];
		pwqConditions[i].channelB = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j + 1];
		pwqConditions[i].channelC = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j + 2];
		pwqConditions[i].channelD = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j + 3];
		pwqConditions[i].external = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j + 4];
		pwqConditions[i].aux      = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j + 5];

		j = j + 6;
	}

	status = ps5000aSetPulseWidthQualifier(handle, pwqConditions, nConditions, (PS5000A_THRESHOLD_DIRECTION) direction, lower, upper, (PS5000A_PULSE_WIDTH_TYPE) type);
	free(pwqConditions);
	return status;
}

/****************************************************************************
* setChannelCount
*
* Sets the number of analogue channels and digital ports on the device by
* querying the model number.
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 setChannelCount(int16_t handle, int16_t channelCount)
{
	int8_t variant[15];
	int16_t requiredSize = 0;
	PICO_STATUS status = PICO_OK;
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	// Obtain the model number
	status = ps5000aGetUnitInfo(handle, variant, sizeof(variant), &requiredSize, PICO_VARIANT_INFO);

	if (status == PICO_OK)
	{
		// Set the number of analogue channels
		dev->channelCount = (int16_t) variant[1];
		dev->channelCount = dev->channelCount - 48; // Subtract ASCII 0 (48)

		// Determine if the device is an MSO
		if (strstr((const char *)variant, "MSO") != NULL)
		{
			dev->digitalPortCount = 2;
		}
		else
		{
			dev->digitalPortCount = 0;
		}
	}

	return status;
}

/****************************************************************************
* setEnabledChannels
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 setEnabledChannels(int16_t handle, int16_t * enabledChannels)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	if (dev->channelCount > 0 && dev->channelCount <= PS5000A_MAX_CHANNELS)
	{
		memcpy_s((int16_t *)dev->enabledChannels, PS5000A_MAX_CHANNELS * sizeof(int16_t),
			(int16_t *)enabledChannels, PS5000A_MAX_CHANNELS * sizeof(int16_t));

		return PICO_OK;
	}

	return PICO_INVALID_PARAMETER;
}

/****************************************************************************
* setAppAndDriverBuffers
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 setAppAndDriverBuffers(int16_t handle, PS5000A_CHANNEL channel, int16_t * appBuffer, int16_t * driverBuffer, uint32_t bufferLength)
{
	// Map port number to internal enumeration for MSO devices.
	PS5000A_WRAP_DIGITAL_PORT_INDEX portIndex = PS5000A_WRAP_DIGITAL_PORT0;
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	if (bufferLength <= 0)
	{
		return PICO_INVALID_PARAMETER;
	}

	if (channel == PS5000A_DIGITAL_PORT0 || channel == PS5000A_DIGITAL_PORT1)
	{
		if (channel == PS5000A_DIGITAL_PORT0)
		{
			portIndex = PS5000A_WRAP_DIGITAL_PORT0;
		}
		else
		{
			portIndex = PS5000A_WRAP_DIGITAL_PORT1;
		}

		dev->wrapBufferInfo.appDigiBuffers[portIndex * 2] = appBuffer;
		dev->wrapBufferInfo.driverDigiBuffers[portIndex * 2] = driverBuffer;

		dev->wrapBufferInfo.digiBufferLengths[portIndex] = bufferLength;

		return PICO_OK;
	}
	else if (channel < PS5000A_CHANNEL_A || channel >= PS5000A_MAX_CHANNELS)
	{
		return PICO_INVALID_CHANNEL;
	}
	else
	{
		dev->wrapBufferInfo.appBuffers[channel * 2] = appBuffer;
		dev->wrapBufferInfo.driverBuffers[channel * 2] = driverBuffer;

		dev->wrapBufferInfo.bufferLengths[channel] = bufferLength;

		return PICO_OK;
	}
}

/****************************************************************************
* setMaxMinAppAndDriverBuffers
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 setMaxMinAppAndDriverBuffers(int16_t handle, PS5000A_CHANNEL channel, int16_t * appMaxBuffer, int16_t * appMinBuffer, int16_t * driverMaxBuffer, int16_t * driverMinBuffer, uint32_t bufferLength)
{
	// Map port number to internal enumeration for MSO devices.
	PS5000A_WRAP_DIGITAL_PORT_INDEX portIndex = PS5000A_WRAP_DIGITAL_PORT0;
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	if (bufferLength <= 0)
	{
		return PICO_INVALID_PARAMETER;
	}

	if (channel == PS5000A_DIGITAL_PORT0 || channel == PS5000A_DIGITAL_PORT1)
	{
		if (channel == PS5000A_DIGITAL_PORT0)
		{
			portIndex = PS5000A_WRAP_DIGITAL_PORT0;
		}
		else
		{
			portIndex = PS5000A_WRAP_DIGITAL_PORT1;
		}

		dev->wrapBufferInfo.appDigiBuffers[portIndex * 2] = appMaxBuffer;
		dev->wrapBufferInfo.driverDigiBuffers[portIndex * 2] = driverMaxBuffer;

		dev->wrapBufferInfo.appDigiBuffers[portIndex * 2 + 1] = appMinBuffer;
		dev->wrapBufferInfo.driverDigiBuffers[portIndex * 2 + 1] = driverMinBuffer;

		dev->wrapBufferInfo.digiBufferLengths[portIndex] = bufferLength;

		return PICO_OK;
	}
	else if (channel < PS5000A_CHANNEL_A || channel >= PS5000A_MAX_CHANNELS)
	{
		return PICO_INVALID_CHANNEL;
	}
	else
	{
		dev->wrapBufferInfo.appBuffers[channel * 2] = appMaxBuffer;
		dev->wrapBufferInfo.driverBuffers[channel * 2] = driverMaxBuffer;

		dev->wrapBufferInfo.appBuffers[channel * 2 + 1] = appMinBuffer;
		dev->wrapBufferInfo.driverBuffers[channel * 2 + 1] = driverMinBuffer;

		dev->wrapBufferInfo.bufferLengths[channel] = bufferLength;

		return PICO_OK;
	}
}

/****************************************************************************
* setEnabledDigitalPorts
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 setEnabledDigitalPorts(int16_t handle, int16_t * enabledDigitalPorts)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	if (dev->digitalPortCount == 0 || dev->digitalPortCount == PS5000A_WRAP_MAX_DIGITAL_PORTS)
	{
		memcpy_s((int16_t *) dev->enabledDigitalPorts, PS5000A_WRAP_MAX_DIGITAL_PORTS * sizeof(int16_t),
			(int16_t *) enabledDigitalPorts, PS5000A_WRAP_MAX_DIGITAL_PORTS * sizeof(int16_t));

		return PICO_OK;
	}

	return PICO_INVALID_PARAMETER;
}

/****************************************************************************
* getOverflow
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 getOverflow(int16_t handle, int16_t * overflow)
{
	WRAP_DEVICE_INFO * dev = findDevice(handle);

	if (dev == NULL)
	{
		return PICO_INVALID_HANDLE;
	}

	*overflow = dev->overflow;
	return PICO_OK;
}

/****************************************************************************
* SetTriggerConditionsV2
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetTriggerConditionsV2(int16_t handle, int32_t *conditionsArray, int16_t nConditions, PS5000A_CONDITIONS_INFO info)
{
	PICO_STATUS status;
	int16_t i = 0;
	int16_t j = 0;

	PS5000A_CONDITION *conditions = (PS5000A_CONDITION *)calloc(nConditions, sizeof(PS5000A_CONDITION));

	for (i = 0; i < nConditions; i++)
	{
		conditions[i].source = (PS5000A_CHANNEL)conditionsArray[j];
		conditions[i].condition = (PS5000A_TRIGGER_STATE)conditionsArray[j + 1];

		j = j + 2;
	}
	status = ps5000aSetTriggerChannelConditionsV2(handle, conditions, nConditions, info);
	free(conditions);

	return status;
}


/****************************************************************************
* SetTriggerDirectionsV2
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetTriggerDirectionsV2(int16_t handle, int32_t * directionsArray, int16_t nDirections)
{
	PICO_STATUS status;
	int16_t i = 0;
	int16_t j = 0;

	PS5000A_DIRECTION *directions = (PS5000A_DIRECTION *)calloc(nDirections, sizeof(PS5000A_DIRECTION));

	for (i = 0; i < nDirections; i++)
	{
		directions[i].source = (PS5000A_CHANNEL) directionsArray[j];
		directions[i].direction = (PS5000A_THRESHOLD_DIRECTION) directionsArray[j + 1];
		directions[i].mode = (PS5000A_THRESHOLD_MODE) directionsArray[j + 2];

		j = j + 3;
	}

	status = ps5000aSetTriggerChannelDirectionsV2(handle, directions, (uint16_t)nDirections);
	free(directions);

	return status;
}

/****************************************************************************
* SetTriggerPropertiesV2
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetTriggerPropertiesV2(int16_t handle, int32_t *propertiesArray, int16_t nProperties)
{
	int16_t i;
	int16_t j = 0;
	int16_t auxEnable = 0;
	PICO_STATUS status;

	PS5000A_TRIGGER_CHANNEL_PROPERTIES_V2 *channelProperties = (PS5000A_TRIGGER_CHANNEL_PROPERTIES_V2 *)calloc(nProperties, sizeof(PS5000A_TRIGGER_CHANNEL_PROPERTIES_V2));

	for (i = 0; i < nProperties; i++)
	{
		channelProperties[i].thresholdUpper = propertiesArray[j];
		channelProperties[i].thresholdUpperHysteresis = propertiesArray[j + 1];
		channelProperties[i].thresholdLower = propertiesArray[j + 2];
		channelProperties[i].thresholdLowerHysteresis = propertiesArray[j + 3];
		channelProperties[i].channel = (PS5000A_CHANNEL) propertiesArray[j + 4];

		j = j + 5;
	}

	status = ps5000aSetTriggerChannelPropertiesV2(handle, channelProperties, nProperties, auxEnable);
	free(channelProperties);

	return status;
}

/****************************************************************************
* SetTriggerDigitalPortProperties
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetTriggerDigitalPortProperties(int16_t handle, int32_t *digitalDirections, int16_t nDirections)
{
	int16_t i;
	int16_t j = 0;
	PICO_STATUS status;

	PS5000A_DIGITAL_CHANNEL_DIRECTIONS *directions = (PS5000A_DIGITAL_CHANNEL_DIRECTIONS *)calloc(nDirections, sizeof(PS5000A_DIGITAL_CHANNEL_DIRECTIONS));

	for (i = 0; i < nDirections; i++)
	{
		directions[i].channel = digitalDirections[j];
		directions[i].direction = digitalDirections[j + 1];

		j = j + 2;
	}

	status = ps5000aSetTriggerDigitalPortProperties(handle, directions, nDirections);
	free(directions);

	return status;
}

/****************************************************************************
* SetPulseWidthQualifierConditions
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetPulseWidthQualifierConditions(int16_t handle, int32_t *pwqConditionsArray, int16_t nConditions, PS5000A_CONDITIONS_INFO info)
{
	PICO_STATUS status;
	int16_t i = 0;
	int16_t j = 0;

	PS5000A_CONDITION * pwqConditions = (PS5000A_CONDITION *)calloc(nConditions, sizeof(PS5000A_CONDITION));

	for (i = 0; i < nConditions; i++)
	{
		pwqConditions[i].source = (PS5000A_CHANNEL) pwqConditionsArray[j];
		pwqConditions[i].condition = (PS5000A_TRIGGER_STATE) pwqConditionsArray[j + 1];

		j = j + 2;
	}

	status = ps5000aSetPulseWidthQualifierConditions(handle, pwqConditions, nConditions, info);
	free(pwqConditions);

	return status;
}

/****************************************************************************
* SetPulseWidthQualifierDirections
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetPulseWidthQualifierDirections(int16_t handle, int32_t * pwqDirectionsArray, int16_t nDirections)
{
	PICO_STATUS status;
	int16_t i = 0;
	int16_t j = 0;

	PS5000A_DIRECTION * pwqDirections = (PS5000A_DIRECTION *)calloc(nDirections, sizeof(PS5000A_DIRECTION));

	for (i = 0; i < nDirections; i++)
	{
		pwqDirections[i].source = (PS5000A_CHANNEL) pwqDirectionsArray[j];
		pwqDirections[i].direction = (PS5000A_THRESHOLD_DIRECTION) pwqDirectionsArray[j + 1];
		pwqDirections[i].mode = (PS5000A_THRESHOLD_MODE) pwqDirectionsArray[j + 2];

		j = j + 3;
	}

	status = ps5000aSetPulseWidthQualifierDirections(handle, pwqDirections, nDirections);
	free(pwqDirections);

	return status;
}

/****************************************************************************
* SetPulseWidthDigitalPortProperties
****************************************************************************/
extern PICO_STATUS PREF0 PREF1 SetPulseWidthDigitalPortProperties(int16_t handle, int32_t *pwqDigitalDirections, int16_t nDirections)
{
	int16_t i;
	int16_t j = 0;
	PICO_STATUS status;

	PS5000A_DIGITAL_CHANNEL_DIRECTIONS *pwqDirections = (PS5000A_DIGITAL_CHANNEL_DIRECTIONS *)calloc(nDirections, sizeof(PS5000A_DIGITAL_CHANNEL_DIRECTIONS));

	for (i = 0; i < nDirections; i++)
	{
		pwqDirections[i].channel = pwqDigitalDirections[j];
		pwqDirections[i].direction = pwqDigitalDirections[j + 1];

		j = j + 2;
	}

	status = ps5000aSetPulseWidthDigitalPortProperties(handle, pwqDirections, nDirections);
	free(pwqDirections);

	return status;
}