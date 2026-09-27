/**
 * @file hl_cam_bus.h
 * @brief Release CubeMX I2C1 so camera BSP (hbus_i2c1, polling) owns the bus.
 */
#ifndef HL_CAM_BUS_H
#define HL_CAM_BUS_H

#ifdef __cplusplus
extern "C" {
#endif

void hl_cam_bus_prep(void);
void hl_cam_power_on(void);

#ifdef __cplusplus
}
#endif

#endif /* HL_CAM_BUS_H */
