# Azure Sphere HVAC Solution

## Clone Workshop

```bash
git clone --recurse-submodules https://github.com/gloveboxes/AzureSphereWorkshop.git AzureSphereWorkshop
```

We choose an HVAC system for the scenario as they are familiar systems found in our homes and workplaces.
An HVAC system is a Heating Ventilation, Air Conditioning Unit.
https://en.wikipedia.org/wiki/Heating,_ventilation,_and_air_conditioning

The Azure Sphere device will act as an HVAC system. This works well as several Azure Sphere developer kits
include built-in sensors for monitoring temperature and pressure.
For developer kits without sensors then it is easy to simulate those sensors.

![HVAC System](https://upload.wikimedia.org/wikipedia/commons/9/90/Rooftop_Packaged_Units.JPG)

## IoT concepts covered

We cover the following IoT concepts in this scenario:

1. Publishing telemetry:
    * The current temperature, air pressure, and humidity
    * The HVACs current operating state - Off, Cooling, and Heating
2. Data validation
    * Cloud to device data validation
    * Reporting sensor data out of range faults
3. Remote operations
    * Setting the HVAC target room temperature
    * Setting the HVAC telemetry publish rate
    * Setting the HVAC display panel user message
    * Turning the HVAC on and off
    * Restarting the HVAC unit
4. Integrating real-time sensors
    * Upgrade HVAC with a real-time sensor
5. Predictive Maintenance with AI
    * Predict HVAC compressor issues with an Edge Impulse AI model
6. Production
    * Reporting sw version, startup utc
    * A watchdog to check the heath of HVAC system and restart if needed.
    * How to package and update your applications in production
    * How to pick the best time to update your application with Deferred Updates
7. End to end - bringing it altogether
    * Bring all the pieces together for a comprehensive solution
