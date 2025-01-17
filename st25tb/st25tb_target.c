/*  Benjamin DELPY `gentilkiwi`
    https://blog.gentilkiwi.com
    benjamin@gentilkiwi.com
    Licence : https://creativecommons.org/licenses/by/4.0/
*/
#include "st25tb_target.h"

const uint8_t st25tb_ui8ChipId = 0x42;
const uint8_t ST25TB_TARGET_KIWI_SPECIAL_RETCODE_OK[] = {0xca, 0xfe, 0xba, 0xbe}, ST25TB_TARGET_KIWI_SPECIAL_RETCODE_KO[] = {0xde, 0xca, 0xfb, 0xad};

volatile tSt25TbState g_eCurrentTargetState;

void ST25TB_Target_Init()
{
    ST25TB_CARDS_fromSlot(0);
    g_eCurrentTargetState = PowerOff;
}

void ST25TB_Target_ResetState()
{
    g_eCurrentTargetState = PowerOff;
}

uint8_t ST25TB_Target_AdjustIdxForSpecialAddr(uint8_t original)
{
    switch(original)
    {
    case 0xff:
        return ST25TB_CARDS_INDEX_SYSTEM;
    case 0x8e:
        return ST25TB_CARDS_INDEX_UID;
    case 0x8f:
        return ST25TB_CARDS_INDEX_UID_2;
    default:
        return original;
    }
}

tSt25TbState ST25TB_Target_StateMachine()
{
    uint8_t cbData = 0, idx, delay;
    const uint8_t *pcbData = 0;

    switch (g_eCurrentTargetState)
    {
    case PowerOff:
    case Ready:

        if ((g_ui8cbFifoBuffer == 2) && (g_ui8FifoBuffer[0] == ST25TB_CMD_INITIATE))
        {
            g_eCurrentTargetState = Inventory;
            pcbData = &st25tb_ui8ChipId;
            cbData = sizeof(st25tb_ui8ChipId);
            delay = ST25TB_TARGET_GLOBAL_DELAY_US + ST25TB_TARGET_MEDIUM_DELAY_US;
        }
        else
        {
            g_eCurrentTargetState = Invalid;
        }

        break;

    case Inventory:

        if ((g_ui8cbFifoBuffer == 2) && (g_ui8FifoBuffer[0] == ST25TB_CMD_SELECT))
        {
            if (g_ui8FifoBuffer[1] == st25tb_ui8ChipId)
            {
                g_eCurrentTargetState = Selected;
            }
            pcbData = &st25tb_ui8ChipId;
            cbData = sizeof(st25tb_ui8ChipId);
            delay = ST25TB_TARGET_GLOBAL_DELAY_US + ST25TB_TARGET_MEDIUM_DELAY_US;
        }
        else if ((g_ui8cbFifoBuffer >= 1) && ((g_ui8FifoBuffer[0] & 0x0f) == ST25TB_CMD_SLOT_MARKER_MASK)) // Slot_marker() mask includes Initiate() and Pcall16()
        {
            pcbData = &st25tb_ui8ChipId;
            cbData = sizeof(st25tb_ui8ChipId);
            delay = ST25TB_TARGET_GLOBAL_DELAY_US + ST25TB_TARGET_MEDIUM_DELAY_US;
        }
        else
        {
            g_eCurrentTargetState = PowerOff;
        }

        break;

    case Selected:

        if (g_ui8cbFifoBuffer == 1)
        {
            if (g_ui8FifoBuffer[0] == ST25TB_CMD_GET_UID)
            {
                pcbData = ST25TB_CARDS_CurrentCard[ST25TB_CARDS_INDEX_UID];
                cbData = 2 * sizeof(ST25TB_CARDS_CurrentCard[0]);
                delay = ST25TB_TARGET_GLOBAL_DELAY_US;
            }
            else if (g_ui8FifoBuffer[0] == ST25TB_CMD_RESET_TO_INVENTORY)
            {
                g_eCurrentTargetState = Inventory;
            }
            else if (g_ui8FifoBuffer[0] == ST25TB_CMD_COMPLETION)
            {
                g_eCurrentTargetState = Deactivated;
            }
        }
        else if (g_ui8cbFifoBuffer == 2)
        {
            if (g_ui8FifoBuffer[0] == ST25TB_CMD_READ_BLOCK)
            {
                if (g_ui8FifoBuffer[1] < ST25TB_BLOCK_NUM || g_ui8FifoBuffer[1] == 0xff || g_ui8FifoBuffer[1] == 0x8e || g_ui8FifoBuffer[1] == 0x8f)
                {
                    idx = ST25TB_Target_AdjustIdxForSpecialAddr(g_ui8FifoBuffer[1]);
                    pcbData = ST25TB_CARDS_CurrentCard[idx];
                    cbData = sizeof(ST25TB_CARDS_CurrentCard[0]);
                    delay = ST25TB_TARGET_GLOBAL_DELAY_US + ST25TB_TARGET_SMALL_DELAY_US;
                }
            }
        }
        else if ((g_ui8cbFifoBuffer == 6) && (g_ui8FifoBuffer[0] == ST25TB_CMD_WRITE_BLOCK))
        {
            if (g_ui8FifoBuffer[1] < ST25TB_BLOCK_NUM || g_ui8FifoBuffer[1] == 0xff || g_ui8FifoBuffer[1] == 0x8e || g_ui8FifoBuffer[1] == 0x8f)
            {
                idx = ST25TB_Target_AdjustIdxForSpecialAddr(g_ui8FifoBuffer[1]);
                *(uint32_t *) ST25TB_CARDS_CurrentCard[idx] = *(uint32_t *) (g_ui8FifoBuffer + 2);
            }
            else if(g_ui8FifoBuffer[1] == 0x99)
            {
                ST25TB_CARDS_toSlot(0); // maybe move at the end for Flash operation ? (slow, even if ret is not really needed)
                pcbData = ST25TB_TARGET_KIWI_SPECIAL_RETCODE_OK;
                cbData = sizeof(ST25TB_TARGET_KIWI_SPECIAL_RETCODE_OK);
                delay = ST25TB_TARGET_GLOBAL_DELAY_US;
            }
        }

        break;

    case Deselected:

        if ((g_ui8cbFifoBuffer == 2) && (g_ui8FifoBuffer[0] == ST25TB_CMD_SELECT))
        {
            if (g_ui8FifoBuffer[1] == st25tb_ui8ChipId)
            {
                g_eCurrentTargetState = Selected;
            }
            pcbData = &st25tb_ui8ChipId;
            cbData = sizeof(st25tb_ui8ChipId);
            delay = ST25TB_TARGET_GLOBAL_DELAY_US + ST25TB_TARGET_MEDIUM_DELAY_US;
        }
        else
        {
            g_eCurrentTargetState = PowerOff;
        }

        break;

    case Deactivated:
    default:

        g_eCurrentTargetState = PowerOff;

        break;
    }

    if (pcbData && cbData)
    {
        LP_TIMER_delay_Microsecond(delay);
        if(!ST25TB_Send(pcbData, cbData))
        {
            g_eCurrentTargetState = Invalid;
        }
    }
    else if(g_eCurrentTargetState != Invalid)
    {
        TRF7970A_ignoreCmd();
    }

    return g_eCurrentTargetState;
}
