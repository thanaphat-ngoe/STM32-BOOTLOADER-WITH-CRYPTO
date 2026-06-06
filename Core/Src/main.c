/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "transport-layer.h"
#include "crc8.h"
#include "ring-buffer.h"
#include "crypto.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum AL_State_TypeDef {
	AL_State_BootloaderMode,
	AL_State_SyncronizeMode,
	AL_State_WaitForFirmwareHeader,
	AL_State_VerifyFirmwareHeader,
    AL_State_EraseFlash,
	AL_State_WriteNewFirmwareHeader,
    AL_State_WaitForFirmwareBody,
	AL_State_VerifyFirmwareSignature,
    AL_State_Done
} AL_State_TypeDef;

typedef void (*pFunction)(void);
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
CRC_HandleTypeDef hcrc;
UART_HandleTypeDef huart2;
DMA_HandleTypeDef hdma_usart2_rx;

/* USER CODE BEGIN PV */
static AL_State_TypeDef al_state = AL_State_BootloaderMode;

static TL_Packet_TypeDef temp_packet;

static TIMER_TypeDef timer;

static RB_TypeDef ring_buffer = {
	.Buffer = 0,
	.Mask = 0,
	.ReadIndex = 0,
	.WriteIndex = 0
};

static FLASH_EraseInitTypeDef pEraseInit = {
	.TypeErase   = FLASH_TYPEERASE_PAGES,
	.PageAddress = FIRMWARE_IMAGE_START_ADDRESS,
	.NbPages	 = 272
};

static uint8_t data_buffer[128] = {0U};

static uint32_t bytes_written = 0;

static uint32_t PageError = 0;

static uint32_t firmware_header_byte_receive = 0;

static FirmwareHeader_TypeDef* ptr_existing_firmware_header = (FirmwareHeader_TypeDef*)(FIRMWARE_IMAGE_START_ADDRESS);
static FirmwareHeader_TypeDef  tmp_new_firmware_header = {0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */
static void Main_Firmware(void);
static void TIMER_Init(TIMER_TypeDef* Timer, uint32_t WaitTime, bool AutoReset);
static void TIMER_Reset(TIMER_TypeDef* Timer);
static bool TIMER_Is_Elapsed(TIMER_TypeDef* Timer);

// void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  	/* USER CODE BEGIN 1 */

  	/* USER CODE END 1 */

  	/* MCU Configuration--------------------------------------------------------*/

  	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  	HAL_Init();

  	/* USER CODE BEGIN Init */

  	/* USER CODE END Init */

  	/* Configure the system clock */
  	SystemClock_Config();

  	/* USER CODE BEGIN SysInit */

  	/* USER CODE END SysInit */

  	/* Initialize all configured peripherals */
  	MX_GPIO_Init();
  	MX_DMA_Init();
  	MX_USART2_UART_Init();
  	MX_CRC_Init();
 	/* USER CODE BEGIN 2 */
	TIMER_Init(&timer, DEFAULT_TIMEOUT, false);
	TL_Init();
	RING_BUFFER_Init(&ring_buffer, data_buffer, 128);
	HAL_UART_Receive_DMA(&huart2, ring_buffer.Buffer, 128);
  	/* USER CODE END 2 */

  	/* Infinite loop */
  	/* USER CODE BEGIN WHILE */
	while (al_state != AL_State_Done) 
	{
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		while (!TL_Update(&ring_buffer)) 
		{
			if (TIMER_Is_Elapsed(&timer)) 
            {
                al_state = AL_State_Done; 
                break;
            }
		}

        switch (al_state) 
		{
			case AL_State_BootloaderMode: 
			{
				TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_BOOTLOADER_MODE);
                TL_Write(&temp_packet, true);
                TIMER_Reset(&timer);
                al_state = AL_State_SyncronizeMode;
			} 
			break;
			
			case AL_State_SyncronizeMode:
			{
                if (TL_Is_Packet_Available()) 
                {
                    TL_Read(&temp_packet);
                    if (TL_Verify_Command_Packet(&temp_packet, AL_MESSAGE_SYNCHRONIZE_MODE)) 
                    {
                        TIMER_Reset(&timer);
                        al_state = AL_State_WaitForFirmwareHeader;
                    }
                } 
                else if (TIMER_Is_Elapsed(&timer)) 
                {
                    al_state = AL_State_Done; 
                }
			} break;

			case AL_State_WaitForFirmwareHeader: 
			{
				if (TL_Is_Packet_Available()) 
                {
                    TIMER_Reset(&timer);

                    TL_Read(&temp_packet);
                    if (TL_Verify_Command_Packet(&temp_packet, AL_MESSAGE_SENT_NEW_FIRMWARE_HEADER)) 
                    {
                        memcpy((uint8_t*)(&tmp_new_firmware_header) + firmware_header_byte_receive, temp_packet.PacketData, PACKET_DATA_BYTE_SIZE);
                        firmware_header_byte_receive = firmware_header_byte_receive + PACKET_DATA_BYTE_SIZE;
                    }
                    else
                    {
                        TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
                        TL_Write(&temp_packet, false);
                        al_state = AL_State_Done;
                        continue;
                    }
                    
                    if (firmware_header_byte_receive == sizeof(FirmwareHeader_TypeDef)) 
                    {
                        al_state = AL_State_VerifyFirmwareHeader;
                    } 
                    else 
                    {
                        continue;
                    }
                } 
                else if (TIMER_Is_Elapsed(&timer)) 
                {
                    al_state = AL_State_Done;
                }
			} break;

			case AL_State_VerifyFirmwareHeader: 
			{
				if (tmp_new_firmware_header.MagicNumber != MAGIC_NUMBER) 
				{
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
					TL_Write(&temp_packet, false);
					al_state = AL_State_Done;
					continue;
				}

				if (tmp_new_firmware_header.DeviceID != DEVICE_ID) 
				{
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
					TL_Write(&temp_packet, false);
					al_state = AL_State_Done;
					continue;
				}

				if (!(tmp_new_firmware_header.Version > ptr_existing_firmware_header->Version)) 
				{
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
					TL_Write(&temp_packet, false);
					al_state = AL_State_Done;
					continue;
				}

				if (tmp_new_firmware_header.Size > (MAX_FIRMWARE_IMAGE_SIZE - sizeof(FirmwareHeader_TypeDef))) 
				{
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
					TL_Write(&temp_packet, false);
					al_state = AL_State_Done;
					continue;
				} 

				if ((tmp_new_firmware_header.Size % 4) != 0) 
				{
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
					TL_Write(&temp_packet, false);
					al_state = AL_State_Done;
					continue;
				}

				for (uint8_t i = 0; i < 44; i++) 
				{
					if (tmp_new_firmware_header.Reserved[i] != 0xFFFFFFFF) 
					{ 
						TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
						TL_Write(&temp_packet, false);
						al_state = AL_State_Done;
						continue;
					} 
				} 

				TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_VERIFIED_NEW_FIRMWARE_HEADER);
                TL_Write(&temp_packet, true);
                TIMER_Reset(&timer); 
                al_state = AL_State_EraseFlash;
			} break;

            case AL_State_EraseFlash: {
                HAL_FLASH_Unlock();
                HAL_FLASHEx_Erase(&pEraseInit, &PageError);
                HAL_FLASH_Lock();
				TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ERASED_FLASH);
                TL_Write(&temp_packet, true);
				TIMER_Reset(&timer);
				al_state = AL_State_WriteNewFirmwareHeader; 
            } break;

			case AL_State_WriteNewFirmwareHeader: {
                HAL_FLASH_Unlock();
				for (uint32_t i = 0; i < sizeof(FirmwareHeader_TypeDef) / 4; i++) {
                    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FIRMWARE_IMAGE_START_ADDRESS + (i * 4), *(((uint32_t*)&tmp_new_firmware_header) + i));
                }
                HAL_FLASH_Lock();
				TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_WROTE_NEW_FIRMWARE_HEADER);
                TL_Write(&temp_packet, true);
				TIMER_Reset(&timer);
				al_state = AL_State_WaitForFirmwareBody; 
			} break;

            case AL_State_WaitForFirmwareBody: {
                if (TL_Is_Packet_Available()) {
					TIMER_Reset(&timer);
                    
					TL_Read(&temp_packet);
					uint32_t firmware_data = (
						(temp_packet.PacketData[0])       |
						(temp_packet.PacketData[1] << 8)  |
						(temp_packet.PacketData[2] << 16) |
						(temp_packet.PacketData[3] << 24) 
					);
					HAL_FLASH_Unlock();
					HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FIRMWARE_ENTRY_POINT_ADDRESS + bytes_written, firmware_data);
					HAL_FLASH_Lock();
					bytes_written += 4;
                    
                    if (bytes_written == tmp_new_firmware_header.Size) {
						bytes_written = 0;
                   	 	al_state = AL_State_VerifyFirmwareSignature;
                    }
                }
                else if (TIMER_Is_Elapsed(&timer)) 
                {
					HAL_FLASH_Unlock();
					HAL_FLASHEx_Erase(&pEraseInit, &PageError);
					HAL_FLASH_Lock();
                    al_state = AL_State_Done;
                }
            } break;

			case AL_State_VerifyFirmwareSignature: {
				if (Verify_Firmware_Signature(FIRMWARE_IMAGE_START_ADDRESS)) {
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_UPDATE_SUCCESSFUL);
                	TL_Write(&temp_packet, true);
                    al_state = AL_State_Done;
				} else {
					HAL_FLASH_Unlock();
                	HAL_FLASHEx_Erase(&pEraseInit, &PageError);
					HAL_FLASH_Lock();
					TL_Create_Command_Packet(&temp_packet, AL_MESSAGE_ABORT_OPERATION);
                	TL_Write(&temp_packet, true);
                    al_state = AL_State_Done;
				}
			} break;

			case AL_State_Done: {
				break;
			}

			default: {
				al_state = AL_State_BootloaderMode;
			}
        }
	}
	
	HAL_Delay(1000);
	Main_Firmware();
  	/* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

	/** Configure the main internal regulator output voltage
	*/
	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

	/** Initializes the RCC Oscillators according to the specified parameters
	* in the RCC_OscInitTypeDef structure.
	*/
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
	RCC_OscInitStruct.HSIState = RCC_HSI_ON;
	RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
	RCC_OscInitStruct.PLL.PLLMUL = RCC_PLLMUL_4;
	RCC_OscInitStruct.PLL.PLLDIV = RCC_PLLDIV_2;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	*/
	RCC_ClkInitStruct.ClockType = 
		RCC_CLOCKTYPE_HCLK   |
		RCC_CLOCKTYPE_SYSCLK |
		RCC_CLOCKTYPE_PCLK1	 |
		RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
	{
		Error_Handler();
	}
	PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
	PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
	{
		Error_Handler();
	}
}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{
	/* USER CODE BEGIN CRC_Init 0 */

	/* USER CODE END CRC_Init 0 */

	/* USER CODE BEGIN CRC_Init 1 */

	/* USER CODE END CRC_Init 1 */
	hcrc.Instance = CRC;
	hcrc.Init.DefaultPolynomialUse = DEFAULT_POLYNOMIAL_ENABLE;
	hcrc.Init.DefaultInitValueUse = DEFAULT_INIT_VALUE_ENABLE;
	hcrc.Init.InputDataInversionMode = CRC_INPUTDATA_INVERSION_NONE;
	hcrc.Init.OutputDataInversionMode = CRC_OUTPUTDATA_INVERSION_DISABLE;
	hcrc.InputDataFormat = CRC_INPUTDATA_FORMAT_BYTES;
	if (HAL_CRC_Init(&hcrc) != HAL_OK)
	{
		Error_Handler();
	}
	/* USER CODE BEGIN CRC_Init 2 */

	/* USER CODE END CRC_Init 2 */
}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART2_UART_Init(void)
{
	/* USER CODE BEGIN USART2_Init 0 */

	/* USER CODE END USART2_Init 0 */

	/* USER CODE BEGIN USART2_Init 1 */

	/* USER CODE END USART2_Init 1 */
	huart2.Instance = USART2;
	huart2.Init.BaudRate = 115200;
	huart2.Init.WordLength = UART_WORDLENGTH_8B;
	huart2.Init.StopBits = UART_STOPBITS_1;
	huart2.Init.Parity = UART_PARITY_NONE;
	huart2.Init.Mode = UART_MODE_TX_RX;
	huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
	huart2.Init.OverSampling = UART_OVERSAMPLING_16;
	huart2.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
	huart2.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
	if (HAL_UART_Init(&huart2) != HAL_OK)
	{
		Error_Handler();
	}
	/* USER CODE BEGIN USART2_Init 2 */

	/* USER CODE END USART2_Init 2 */
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{
	/* DMA controller clock enable */
	__HAL_RCC_DMA1_CLK_ENABLE();

	/* DMA interrupt init */
	/* DMA1_Channel4_5_6_7_IRQn interrupt configuration */
	HAL_NVIC_SetPriority(DMA1_Channel4_5_6_7_IRQn, 0, 0);
	HAL_NVIC_EnableIRQ(DMA1_Channel4_5_6_7_IRQn);
}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	/* USER CODE BEGIN MX_GPIO_Init_1 */

	/* USER CODE END MX_GPIO_Init_1 */

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOC_CLK_ENABLE();
	__HAL_RCC_GPIOH_CLK_ENABLE();
	__HAL_RCC_GPIOA_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

	/*Configure GPIO pin : B1_Pin */
	GPIO_InitStruct.Pin = B1_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(B1_GPIO_Port, &GPIO_InitStruct);

	/*Configure GPIO pin : LD2_Pin */
	GPIO_InitStruct.Pin = LD2_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(LD2_GPIO_Port, &GPIO_InitStruct);

	/* USER CODE BEGIN MX_GPIO_Init_2 */

	/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
static void Main_Firmware(void) 
{
	VectorTable_TypeDef* vector_table = (VectorTable_TypeDef*)FIRMWARE_ENTRY_POINT_ADDRESS;
	__HAL_RCC_DMA1_CLK_ENABLE();
    __disable_irq();
    HAL_DeInit();
	HAL_RCC_DeInit();
    vector_table->Reset_Handler();
}

static void TIMER_Init(TIMER_TypeDef* Timer, uint32_t WaitTime, bool AutoReset) 
{
    Timer->WaitTime = WaitTime;
    Timer->AutoReset = AutoReset;
    Timer->TargetTime = HAL_GetTick() + WaitTime;
    Timer->HasElapsed = false;
}

static void TIMER_Reset(TIMER_TypeDef* Timer) 
{
    TIMER_Init(Timer, Timer->WaitTime, Timer->AutoReset);
}

static bool TIMER_Is_Elapsed(TIMER_TypeDef* Timer) 
{
    uint32_t now = HAL_GetTick();
    bool IsElapsed = now >= Timer->TargetTime;

    if (Timer->HasElapsed) return false;

    if (IsElapsed) {
        if (Timer->AutoReset) {
            uint32_t drift = now - Timer->TargetTime;
            Timer->TargetTime = (now + Timer->WaitTime) - drift;
        } else {
            Timer->HasElapsed = true;
        }
    }
    
    return IsElapsed;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
  	/* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
		ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  	/* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
