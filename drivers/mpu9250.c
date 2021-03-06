#include "mpu9250.h"
#include "hal/hal_conf.h"
#include "config.h"

extern I2C_HandleTypeDef hi2c;

static int16_t swx, swy, swz;

static bool readBytes(uint8_t addr, uint8_t subAddr, uint8_t numBytes, uint8_t * buffer) {
	return (HAL_I2C_Mem_Read(&hi2c, addr << 1, subAddr, 1, buffer, numBytes, 500) == HAL_OK) ? false : true;
}

static uint8_t readByte(uint8_t addr, uint8_t subAddr) {
	uint8_t retval;
	HAL_I2C_Mem_Read(&hi2c, addr << 1, subAddr, 1, &retval, 1, 500);
	return retval;
}

static void writeByte(uint8_t addr, uint8_t subAddr, uint8_t data) {
	HAL_I2C_Mem_Write(&hi2c, addr << 1, subAddr, 1, &data, 1, 500);
}

bool mpuAlive(void) {
	return (HAL_I2C_IsDeviceReady(&hi2c, MPU9250_ADDRESS << 1, 1, 500) == HAL_OK) ? true : false;
}

static void delay(int ms) { HAL_Delay(ms); }

int16_t to_cms2(int16_t data) { return (data * 981) / 2048; }

void printMPU9250(void) {

	printf("%d\t(%s)\t0x%02x\r\n", SMPLRT_DIV, "SMPLRT_DIV", readByte(MPU9250_ADDRESS, SMPLRT_DIV));
	printf("%d\t(%s)\t0x%02x\r\n", CONFIG, "CONFIG", readByte(MPU9250_ADDRESS, CONFIG));
	printf("%d\t(%s)\t0x%02x\r\n", ACCEL_CONFIG, "ACCEL_CONFIG", readByte(MPU9250_ADDRESS, ACCEL_CONFIG));
	printf("%d\t(%s)\t0x%02x\r\n", ACCEL_CONFIG2, "ACCEL_CONFIG2", readByte(MPU9250_ADDRESS, ACCEL_CONFIG2));
	printf("%d\t(%s)\t0x%02x\r\n", LP_ACCEL_ODR, "LP_ACCEL_ODR", readByte(MPU9250_ADDRESS, LP_ACCEL_ODR));
	printf("%d\t(%s)\t0x%02x\r\n", WOM_THR, "WOM_THR", readByte(MPU9250_ADDRESS, WOM_THR));
	printf("%d\t(%s)\t0x%02x\r\n", PWR_MGMT_1, "PWR_MGMT_1", readByte(MPU9250_ADDRESS, PWR_MGMT_1));
	printf("%d\t(%s)\t0x%02x\r\n", PWR_MGMT_2, "PWR_MGMT_2", readByte(MPU9250_ADDRESS, PWR_MGMT_2));
	printf("%d\t(%s)\t%d\r\n", XA_OFFSET_H, "XA_OFFSET",
		(int16_t) (((uint16_t) readByte(MPU9250_ADDRESS, XA_OFFSET_H) << 8)
		| readByte(MPU9250_ADDRESS, XA_OFFSET_L)));
	printf("%d\t(%s)\t%d\r\n", YA_OFFSET_H, "YA_OFFSET",
		(int16_t) (((uint16_t) readByte(MPU9250_ADDRESS, YA_OFFSET_H) << 8)
		| readByte(MPU9250_ADDRESS, YA_OFFSET_L)));
	printf("%d\t(%s)\t%d\r\n", ZA_OFFSET_H, "ZA_OFFSET",
		(int16_t) (((uint16_t) readByte(MPU9250_ADDRESS, ZA_OFFSET_H) << 8)
		| readByte(MPU9250_ADDRESS, ZA_OFFSET_L)));
}

static uint16_t get_packet_count(void) {
	uint16_t retval; uint8_t data[2];

	// read FIFO sample count
	readBytes(MPU9250_ADDRESS, FIFO_COUNTH, 2, data);
	retval = ((uint16_t) data[0] << 8) | data[1];
	return retval / 6;
}

static inline void enable_fifo(void) {
	writeByte(MPU9250_ADDRESS, USER_CTRL, 0x40);
	writeByte(MPU9250_ADDRESS, FIFO_EN, 0x08);
}

static inline void disable_fifo(void) {
	writeByte(MPU9250_ADDRESS, FIFO_EN, 0x00);
}

static inline void reset_fifo(void) {
	writeByte(MPU9250_ADDRESS, USER_CTRL, 0x04);
}

static void average_fifo_data(int16_t *buffer) {
	uint8_t data[6];
	uint16_t packet_count, i, accel_temp[3];
	uint32_t avg_count[3] = {0, 0, 0};

	packet_count = get_packet_count();

	for (i = 0; i < packet_count; i++) {
		/* read data for averaging */
		readBytes(MPU9250_ADDRESS, FIFO_R_W, 6, data);

		/* Form signed 16-bit integer for each sample in FIFO */
		accel_temp[0] = (int16_t) (((int16_t) data[0] << 8) | data[1]);
		accel_temp[1] = (int16_t) (((int16_t) data[2] << 8) | data[3]);
		accel_temp[2] = (int16_t) (((int16_t) data[4] << 8) | data[5]);

		/* Sum individual signed 16-bit biases to get accumulated signed 32-bit biases */
		avg_count[0] += (int32_t) accel_temp[0];
		avg_count[1] += (int32_t) accel_temp[1];
		avg_count[2] += (int32_t) accel_temp[2];
	}

	/* Normalize sums to get average count biases */
	buffer[0] = (avg_count[0] / (int32_t) packet_count) - swx;
	buffer[1] = (avg_count[1] / (int32_t) packet_count) - swy;
	buffer[2] = (avg_count[2] / (int32_t) packet_count) - swz;

}

void fifo_begin_mpu9250(void) {
	reset_fifo();
	disable_fifo();
	enable_fifo();
}

void fifo_read_mpu9250(int16_t * destination) {
	disable_fifo();
	average_fifo_data(destination);
}

bool mpu_gathering = false;
uint32_t mpu_ts;
#define MPU_INT		250
bool mpu9250_handler(int16_t * destination) {
	if (!mpu_gathering) {
		fifo_begin_mpu9250();
		mpu_ts = ticks;
		mpu_gathering = true;
	}
	else if (ticks - mpu_ts > MPU_INT) {
		fifo_read_mpu9250(destination);
		mpu_gathering = false;
		return true;
	}
	return false;
}

bool readAccelData(int16_t * destination) {

	reset_fifo();
	disable_fifo();
	enable_fifo();
	delay(50); // accumulate samples
	disable_fifo();

	average_fifo_data(destination);

	return true;
}

#define XA_FACT	5426
#define YA_FACT	-3726
#define ZA_FACT	7488
void calibrate_mpu9250(void) {
	uint8_t data[6]; // data array to hold accelerometer and gyro x, y, z, data
	uint16_t ii, packet_count;
	int32_t  accel_bias[3] = {0, 0, 0};
	int16_t accel_temp[3] = {0, 0, 0};

	// Configure device for bias calculation
	reset_fifo();
	disable_fifo();
	enable_fifo();
	delay(50); // accumulate samples
	disable_fifo();

	packet_count = get_packet_count();

	for (ii = 0; ii < packet_count; ii++) {

		/* read data for averaging */
		readBytes(MPU9250_ADDRESS, FIFO_R_W, 6, data);

		/* Form signed 16-bit integer for each sample in FIFO */
		accel_temp[0] = (int16_t) (((int16_t) data[0] << 8) | data[1]);
		accel_temp[1] = (int16_t) (((int16_t) data[2] << 8) | data[3]);
		accel_temp[2] = (int16_t) (((int16_t) data[4] << 8) | data[5]);

		/* Sum individual signed 16-bit biases to get accumulated signed 32-bit biases */
		accel_bias[0] += (int32_t) accel_temp[0];
		accel_bias[1] += (int32_t) accel_temp[1];
		accel_bias[2] += (int32_t) accel_temp[2];
	}

	/* Normalize sums to get average count biases */
	accel_bias[0] /= (int32_t) packet_count;
	accel_bias[1] /= (int32_t) packet_count;
	accel_bias[2] /= (int32_t) packet_count;

	swx = accel_bias[0];
	swy = accel_bias[1];
	swz = accel_bias[2];
}

bool initMPU9250(void) {

	/* reset device  */
	writeByte(MPU9250_ADDRESS, PWR_MGMT_1, 0x80); delay(100); 

	/* select PLL if available */
	writeByte(MPU9250_ADDRESS, PWR_MGMT_1, 0x01); delay(200);

	/* disable gyros */
	writeByte(MPU9250_ADDRESS, PWR_MGMT_2, 0x07); delay(50); 
	
	/* 20 Hz sample rate */
	writeByte(MPU9250_ADDRESS, SMPLRT_DIV, 0x9);

	// set accel configs
	writeByte(MPU9250_ADDRESS, ACCEL_CONFIG, (AFS_16G << 3));
	writeByte(MPU9250_ADDRESS, ACCEL_CONFIG2, 4);

	writeByte(MPU9250_ADDRESS, INT_ENABLE, 0x00);   // Disable all interrupts
	writeByte(MPU9250_ADDRESS, I2C_MST_CTRL, 0x00); // Disable I2C master

	delay(50);

	calibrate_mpu9250();

	return true;
}

