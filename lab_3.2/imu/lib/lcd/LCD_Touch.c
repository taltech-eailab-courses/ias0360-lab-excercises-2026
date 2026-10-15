#include "LCD_Touch.h"
#include <stdlib.h>
#include <string.h>  
#include <stdio.h>

TP_DEV* pTP_DEV;
TP_DRAW* pTP_Draw;

static void TP_DumpBitmapToSerial(int h, int w, const uint8_t bmp[h][w])
{
    // Build and print one line at a time (faster than putchar per pixel)
    char line[w + 2];           // + '\n' + '\0'
    line[w]   = '\n';
    line[w+1] = '\0';

    for (uint16_t y = 0; y < h; ++y) {
        for (uint16_t x = 0; x < w; ++x) {
            line[x] = bmp[y][x] ? '1' : '0';
        }
        printf("%s", line);         // prints 280 chars of 0/1, then newline
    }
}


static uint16_t TP_Read_ADC(uint8_t CMD)
{
    uint16_t Data = 0;

    //A cycle of at least 400ns.
    DEV_Digital_Write(TP_CS_PIN, 0);

    SPI4W_Write_Byte(CMD);
    Driver_Delay_us(200);

    //	dont write 0xff, it will block xpt2046
    //Data = SPI4W_Read_Byte(0Xff);
    Data = SPI4W_Read_Byte(0X00);
    Data <<= 8; //7bit
    Data |= SPI4W_Read_Byte(0X00);
    //Data = SPI4W_Read_Byte(0Xff);
    Data >>= 3; //5bit
    DEV_Digital_Write(TP_CS_PIN, 1);
    return Data;
}

#define READ_TIMES 5 //Number of readings
#define LOST_NUM 1   //Discard value
static uint16_t 
TP_Read_ADC_Average(uint8_t Channel_Cmd)
{
    uint8_t i, j;
    uint16_t Read_Buff[READ_TIMES];
    uint16_t Read_Sum = 0, Read_Temp = 0;
    //LCD SPI speed = 3 MHz
    spi_set_baudrate(SPI_PORT, 3000000);
    //Read and save multiple samples
    for (i = 0; i < READ_TIMES; i++)
    {
        Read_Buff[i] = TP_Read_ADC(Channel_Cmd);
        Driver_Delay_us(200);
    }
    //LCD SPI speed = 18 MHz
    spi_set_baudrate(SPI_PORT, 18000000);
    //Sort from small to large
    for (i = 0; i < READ_TIMES - 1; i++)
    {
        for (j = i + 1; j < READ_TIMES; j++)
        {
            if (Read_Buff[i] > Read_Buff[j])
            {
                Read_Temp = Read_Buff[i];
                Read_Buff[i] = Read_Buff[j];
                Read_Buff[j] = Read_Temp;
            }
        }
    }

    //Exclude the largest and the smallest
    for (i = LOST_NUM; i < READ_TIMES - LOST_NUM; i++)
        Read_Sum += Read_Buff[i];

    //Averaging
    Read_Temp = Read_Sum / (READ_TIMES - 2 * LOST_NUM);

    return Read_Temp;
}

static void TP_Read_ADC_XY(uint16_t *pXCh_Adc, uint16_t *pYCh_Adc)
{
    *pXCh_Adc = TP_Read_ADC_Average(0xD0);
    *pYCh_Adc = TP_Read_ADC_Average(0x90);
}

#define ERR_RANGE 50 //tolerance scope
static bool TP_Read_TwiceADC(uint16_t *pXCh_Adc, uint16_t *pYCh_Adc)
{
    uint16_t XCh_Adc1, YCh_Adc1, XCh_Adc2, YCh_Adc2;

    //Read the ADC values Read the ADC values twice
    TP_Read_ADC_XY(&XCh_Adc1, &YCh_Adc1);
    Driver_Delay_us(10);
    TP_Read_ADC_XY(&XCh_Adc2, &YCh_Adc2);
    Driver_Delay_us(10);

    //The ADC error used twice is greater than ERR_RANGE to take the average
    if (((XCh_Adc2 <= XCh_Adc1 && XCh_Adc1 < XCh_Adc2 + ERR_RANGE) ||
         (XCh_Adc1 <= XCh_Adc2 && XCh_Adc2 < XCh_Adc1 + ERR_RANGE)) &&
        ((YCh_Adc2 <= YCh_Adc1 && YCh_Adc1 < YCh_Adc2 + ERR_RANGE) ||
         (YCh_Adc1 <= YCh_Adc2 && YCh_Adc2 < YCh_Adc1 + ERR_RANGE)))
    {
        *pXCh_Adc = (XCh_Adc1 + XCh_Adc2) / 2;
        *pYCh_Adc = (YCh_Adc1 + YCh_Adc2) / 2;
        return true;
    }

    //The ADC error used twice is less than ERR_RANGE returns failed
    return false;
}

uint8_t TP_Scan(uint8_t chCoordType)
{
    //In X, Y coordinate measurement, IRQ is disabled and output is low
    if (!DEV_Digital_Read(TP_IRQ_PIN))
    { //Press the button to press
        //Read the physical coordinates
        if (chCoordType)
        {
            TP_Read_TwiceADC(&pTP_DEV->Xpoint, &pTP_DEV->Ypoint);
            //Read the screen coordinates
        }
        else if (TP_Read_TwiceADC(&pTP_DEV->Xpoint, &pTP_DEV->Ypoint))
        {

            if (LCD_2_8 == id)
            {
                pTP_Draw->Xpoint = sLCD_DIS.LCD_Dis_Column -
                                  pTP_DEV->fXfac * pTP_DEV->Xpoint -
                                  pTP_DEV->iXoff;
                pTP_Draw->Ypoint = sLCD_DIS.LCD_Dis_Page -
                                  pTP_DEV->fYfac * pTP_DEV->Ypoint -
                                  pTP_DEV->iYoff;
            }
            else
            {
                //DEBUG("(Xad,Yad) = %d,%d\r\n",pTP_DEV->Xpoint,pTP_DEV->Ypoint);
                if (pTP_DEV->TP_Scan_Dir == R2L_D2U)
                { //Converts the result to screen coordinates
                    pTP_Draw->Xpoint = pTP_DEV->fXfac * pTP_DEV->Xpoint +
                                      pTP_DEV->iXoff;
                    pTP_Draw->Ypoint = pTP_DEV->fYfac * pTP_DEV->Ypoint +
                                      pTP_DEV->iYoff;
                }
                else if (pTP_DEV->TP_Scan_Dir == L2R_U2D)
                {
                    pTP_Draw->Xpoint = sLCD_DIS.LCD_Dis_Column -
                                      pTP_DEV->fXfac * pTP_DEV->Xpoint -
                                      pTP_DEV->iXoff;
                    pTP_Draw->Ypoint = sLCD_DIS.LCD_Dis_Page -
                                      pTP_DEV->fYfac * pTP_DEV->Ypoint -
                                      pTP_DEV->iYoff;
                }
                else if (pTP_DEV->TP_Scan_Dir == U2D_R2L)
                {
                    pTP_Draw->Xpoint = pTP_DEV->fXfac * pTP_DEV->Ypoint +
                                      pTP_DEV->iXoff;
                    pTP_Draw->Ypoint = pTP_DEV->fYfac * pTP_DEV->Xpoint +
                                      pTP_DEV->iYoff;
                }
                else
                {
                    pTP_Draw->Xpoint = sLCD_DIS.LCD_Dis_Column -
                                      pTP_DEV->fXfac * pTP_DEV->Ypoint -
                                      pTP_DEV->iXoff;
                    pTP_Draw->Ypoint = sLCD_DIS.LCD_Dis_Page -
                                      pTP_DEV->fYfac * pTP_DEV->Xpoint -
                                      pTP_DEV->iYoff;
                }
                // DEBUG("( x , y ) = %d,%d\r\n",pTP_Draw->Xpoint,pTP_Draw->Ypoint);
            }
        }
        if (0 == (pTP_DEV->chStatus & TP_PRESS_DOWN))
        { //Not being pressed
            pTP_DEV->chStatus = TP_PRESS_DOWN | TP_PRESSED;
            pTP_DEV->Xpoint0 = pTP_DEV->Xpoint;
            pTP_DEV->Ypoint0 = pTP_DEV->Ypoint;
        }
    }
    else
    {
        if (pTP_DEV->chStatus & TP_PRESS_DOWN)
        {                                  //0x80
            pTP_DEV->chStatus &= ~(1 << 7); //0x00
        }
        else
        {
            pTP_DEV->Xpoint0 = 0;
            pTP_DEV->Ypoint0 = 0;
            pTP_DEV->Xpoint = 0xffff;
            pTP_DEV->Ypoint = 0xffff;
        }
    }

    return (pTP_DEV->chStatus & TP_PRESS_DOWN);
}

void TP_GetAdFac(void)
{
    if (LCD_2_8 == id)
    {
        pTP_DEV->fXfac = 0.066626;
        pTP_DEV->fYfac = 0.089779;
        pTP_DEV->iXoff = -20;
        pTP_DEV->iYoff = -34;
    }
    else
    {
        if (pTP_DEV->TP_Scan_Dir == D2U_L2R)
        { //SCAN_DIR_DFT = D2U_L2R
            pTP_DEV->fXfac = -0.132443;
            pTP_DEV->fYfac = 0.089997;
            pTP_DEV->iXoff = 516;
            pTP_DEV->iYoff = -22;
        }
        else if (pTP_DEV->TP_Scan_Dir == L2R_U2D)
        {
            pTP_DEV->fXfac = 0.089697;
            pTP_DEV->fYfac = 0.134792;
            pTP_DEV->iXoff = -21;
            pTP_DEV->iYoff = -39;
        }
        else if (pTP_DEV->TP_Scan_Dir == R2L_D2U)
        {
            pTP_DEV->fXfac = 0.089915;
            pTP_DEV->fYfac = 0.133178;
            pTP_DEV->iXoff = -22;
            pTP_DEV->iYoff = -38;
        }
        else if (pTP_DEV->TP_Scan_Dir == U2D_R2L)
        {
            pTP_DEV->fXfac = -0.132906;
            pTP_DEV->fYfac = 0.087964;
            pTP_DEV->iXoff = 517;
            pTP_DEV->iYoff = -20;
        }
        else
        {
            LCD_Clear(LCD_BACKGROUND);
            GUI_DisString_EN(0, 60, "Does not support touch-screen \
							calibration in this direction",
                             &Font16, FONT_BACKGROUND, RED);
        }
    }
}


static int count_digits(int number)
{
    if (number == 0)
        return 1;  // 0 has one digit

    number = abs(number);    // make positive
    int count = 0;
    while (number > 0) {
        number /= 10;
        count++;
    }
    return count;
}

void TP_display_input(int h, int w, const uint8_t* src)
{
    for (int i=0; i<h; i++) {
        for (int j=0; j<w; j++) {
            uint8_t num = src[i * w + j];
            int space = 3 - count_digits(num);
            for (int i = 0; i < space; ++i) {
                printf("\xE2\x80\x8A");
            }
            printf("%d", num);
        }
        printf("\n");
    }
}

void TP_DrawHeader(void) 
{
    GUI_DisString_EN(100, 20, "MACHINE LEARNING", &Font24, WHITE, RED);
    GUI_DisString_EN(80, 45, "FOR EMBEDDED SYSTEM", &Font24, WHITE, RED);
}

void TP_Init(LCD_SCAN_DIR Lcd_ScanDir, TP_DEV* tp_dev, TP_DRAW* tp_draw)
{

    DEV_Digital_Write(TP_CS_PIN, 1);

    pTP_DEV = tp_dev;
    pTP_Draw = tp_draw;

    pTP_DEV->TP_Scan_Dir = Lcd_ScanDir;

    TP_Read_ADC_XY(&pTP_DEV->Xpoint, &pTP_DEV->Ypoint);
}
