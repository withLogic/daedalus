/*
Copyright (C) 2001 StrmnNrmn

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"

#include "Interrupt.h"
#include "RSP_HLE.h"
#include "Memory.h"

#include "Debug/DebugLog.h"
#include "Debug/Dump.h"			// For Dump_GetDumpDirectory()

#include "Math/MathUtil.h"

#include "OSHLE/ultra_rcp.h"
#include "OSHLE/ultra_mbi.h"
#include "OSHLE/ultra_sptask.h"

#include "Debug/DBGConsole.h"
#include "Plugins/AudioPlugin.h"
#include "Plugins/GraphicsPlugin.h"

#include "Utility/Profiler.h"
#include "Utility/PrintOpCode.h"

#include "Test/BatchTest.h"

//Do we reaaly need this??? //Salvy
#ifdef DAEDALUS_ENABLE_ASSERTS
volatile bool gRSPHLEActive = false;
#endif

static const bool	gGraphicsEnabled = true;
static const bool	gAudioEnabled	 = true;

#if 0
static void RDP_DumpRSPCode(char * szName, u32 dwCRC, u32 * pBase, u32 dwPCBase, u32 dwLen);
static void RDP_DumpRSPData(char * szName, u32 dwCRC, u32 * pBase, u32 dwPCBase, u32 dwLen);
#endif

#if 0
//*****************************************************************************
//
//*****************************************************************************
void RDP_DumpRSPCode(char * szName, u32 dwCRC, u32 * pBase, u32 dwPCBase, u32 dwLen)
{
	char opinfo[400];
	char szFilePath[MAX_PATH+1];
	char szFileName[MAX_PATH+1];
	FILE * fp;

	Dump_GetDumpDirectory(szFilePath, "rsp_dumps");

	sprintf(szFileName, "task_dump_%s_crc_0x%08x.txt", szName, dwCRC);

	IO::Path::Append(szFilePath, szFileName);


	fp = fopen(szFilePath, "w");
	if (fp == NULL)
		return;

	u32 dwIndex;
	for (dwIndex = 0; dwIndex < dwLen; dwIndex+=4)
	{
		OpCode op;
		u32 pc = dwIndex&0x0FFF;
		op._u32 = pBase[dwIndex/4];

		SprintRSPOpCodeInfo( opinfo, pc + dwPCBase, op );

		fprintf(fp, "0x%08x: <0x%08x> %s\n", pc + dwPCBase, op._u32, opinfo);
		//fprintf(fp, "<0x%08x>\n", dwOpCode);
	}

	fclose(fp);
}
#endif

#if 0
//*****************************************************************************
//
//*****************************************************************************
void RDP_DumpRSPData(char * szName, u32 dwCRC, u32 * pBase, u32 dwPCBase, u32 dwLen)
{
	char szFilePath[MAX_PATH+1];
	char szFileName[MAX_PATH+1];
	FILE * fp;

	Dump_GetDumpDirectory(szFilePath, "rsp_dumps");

	sprintf(szFileName, "task_data_dump_%s_crc_0x%08x.txt", szName, dwCRC);

	IO::Path::Append(szFilePath, szFileName);

	fp = fopen(szFilePath, "w");
	if (fp == NULL)
		return;

	u32 dwIndex;
	for (dwIndex = 0; dwIndex < dwLen; dwIndex+=4)
	{
		u32 dwData;
		u32 pc = dwIndex&0x0FFF;
		dwData = pBase[dwIndex/4];

		fprintf(fp, "0x%08x: 0x%08x\n", pc + dwPCBase, dwData);
	}

	fclose(fp);

}
#endif

//*****************************************************************************
//
//*****************************************************************************
#if 0
static void	RSP_HLE_DumpTaskInfo( const OSTask * pTask )
{
	DBGConsole_Msg(0, "DP: Task:%08x Flags:%08x BootCode:%08x BootCodeSize:%08x",
		pTask->t.type, pTask->t.flags, pTask->t.ucode_boot, pTask->t.ucode_boot_size);

	DBGConsole_Msg(0, "DP: uCode:%08x uCodeSize:%08x uCodeData:%08x uCodeDataSize:%08x",
		pTask->t.ucode, pTask->t.ucode_size, pTask->t.ucode_data, pTask->t.ucode_data_size);

	DBGConsole_Msg(0, "DP: Stack:%08x StackS:%08x Output:%08x OutputS:%08x",
		pTask->t.dram_stack, pTask->t.dram_stack_size, pTask->t.output_buff, pTask->t.output_buff_size);

	DBGConsole_Msg(0, "DP: Data(PC):%08x DataSize:%08x YieldData:%08x YieldDataSize:%08x",
		pTask->t.data_ptr, pTask->t.data_size, pTask->t.yield_data_ptr, pTask->t.yield_data_size);
}
#endif

//*****************************************************************************
//
//*****************************************************************************
void RSP_HLE_Finished(u32 setbits)
{
	// Need to point to last instr?
	//Memory_DPC_SetRegister(DPC_CURRENT_REG, (u32)pTask->t.data_ptr);

	//
	// Set the SP flags appropriately. The RSP is not running anyway, no need to stop it
	//
	u32 status( Memory_SP_SetRegisterBits(SP_STATUS_REG, setbits) );

	//
	// We've set the SP_STATUS_BROKE flag - better check if it causes an interrupt
	//
	if( status & SP_STATUS_INTR_BREAK )
	{
		Memory_MI_SetRegisterBits(MI_INTR_REG, MI_INTR_SP);
		R4300_Interrupt_UpdateCause3();
	}
}

//*****************************************************************************
//
//*****************************************************************************
static EProcessResult RSP_HLE_Graphics()
{
	DAEDALUS_PROFILE( "HLE: Graphics" );

	if (gGraphicsEnabled && gGraphicsPlugin != NULL)
	{
		gGraphicsPlugin->ProcessDList();
	}
	else
	{
		// Skip the entire dlist if graphics are disabled
		Memory_MI_SetRegisterBits(MI_INTR_REG, MI_INTR_DP);
		R4300_Interrupt_UpdateCause3();
	}


#ifdef DAEDALUS_BATCH_TEST_ENABLED
	if (CBatchTestEventHandler * handler = BatchTest_GetHandler())
	{
		handler->OnDisplayListComplete();
	}
#endif

	return PR_COMPLETED;
}

//*****************************************************************************
//
//*****************************************************************************
static EProcessResult RSP_HLE_Audio()
{
	DAEDALUS_PROFILE( "HLE: Audio" );

	if (gAudioEnabled && g_pAiPlugin != NULL)
	{
		return g_pAiPlugin->ProcessAList();
	}
	return PR_COMPLETED;
}

//*****************************************************************************
//
//*****************************************************************************
// RSP_HLE_Jpeg and RSP_HLE_CICX105 were borrowed from Mupen64plus
u32 sum_bytes(const u8 *bytes, u32 size)
{
    u32 sum = 0;
    const u8 * const bytes_end = bytes + size;

    while (bytes != bytes_end)
        sum += *bytes++;

    return sum;
}

//*****************************************************************************
//
//*****************************************************************************
static EProcessResult RSP_HLE_Jpeg(OSTask * task)
{
void jpeg_decode_PS(OSTask *task);
void jpeg_decode_OB(OSTask *task);

	// most ucode_boot procedure copy 0xf80 bytes of ucode whatever the ucode_size is.
	// For practical purpose we use a ucode_size = min(0xf80, task->ucode_size)
	u32 sum = sum_bytes(g_pu8RamBase + (u32)task->t.ucode , Min<u32>(task->t.ucode_size, 0xf80) >> 1);

	//DBGConsole_Msg(0, "JPEG Task: Sum=0x%08x", sum);
	switch(sum)
	{
	case 0x2caa6: // Zelda OOT, Pokemon Stadium {1,2} jpg decompression
		jpeg_decode_PS(task);
		break;
    case 0x130de: // Ogre Battle background decompression
        jpeg_decode_OB(task);
		break;
	}

	return PR_COMPLETED;
}

//*****************************************************************************
//
//*****************************************************************************
static EProcessResult RSP_HLE_CICX105(OSTask * task)
{
    const u32 sum = sum_bytes(g_pu8SpImemBase, 0x1000 >> 1);

    switch(sum)
    {
        /* CIC x105 ucode (used during boot of CIC x105 games) */
        case 0x9e2: /* CIC 6105 */
        case 0x9f2: /* CIC 7105 */
			{
				u32 i;
				u8 * dst = g_pu8RamBase + 0x2fb1f0;
				u8 * src = g_pu8SpImemBase + 0x120;

				/* dma_read(0x1120, 0x1e8, 0x1e8) */
				memcpy(g_pu8SpImemBase + 0x120, g_pu8RamBase + 0x1e8, 0x1f0);

				/* dma_write(0x1120, 0x2fb1f0, 0xfe817000) */
				for (i = 0; i < 24; ++i)
				{
					memcpy(dst, src, 8);
					dst += 0xff0;
					src += 0x8;

				}
			}
			break;

    }

	return PR_COMPLETED;
}
//*****************************************************************************
//
//*****************************************************************************
void RSP_HLE_ProcessTask()
{
	OSTask * pTask = (OSTask *)(g_pu8SpMemBase + 0x0FC0);
#ifdef DAEDALUS_ENABLE_ASSERTS
	const char* task_name= "?";
#endif

	EProcessResult	result( PR_NOT_STARTED );

	//
	// If we want to handle the task, set the pointer
	//
	DAEDALUS_ASSERT( !gRSPHLEActive, "RSP HLE already active, can't run '%s' yet. Status: %08x\n", task_name, Memory_SP_GetRegister(SP_STATUS_REG) );

	// non task
	if(pTask->t.ucode_boot_size > 0x1000)
	{
		RSP_HLE_CICX105(pTask);
		RSP_HLE_Finished(SP_STATUS_BROKE|SP_STATUS_HALT);
		return;
	}

	switch ( pTask->t.type )
	{
		case M_GFXTASK:
			result = RSP_HLE_Graphics();
			break;

		case M_AUDTASK:
			result = RSP_HLE_Audio();
			break;

		case M_VIDTASK:
			// Can't handle
			break;

		case M_JPGTASK:
			result = RSP_HLE_Jpeg(pTask);
			break;

		default:
			// Can't handle
			DBGConsole_Msg(0, "Unknown task: %08x", pTask->t.type );
			//	RSP_HLE_DumpTaskInfo( pTask );
			//	RDP_DumpRSPCode("boot",    0xDEAFF00D, (u32*)(g_pu8RamBase + (((u32)pTask->t.ucode_boot)&0x00FFFFFF)), 0x04001000, pTask->t.ucode_boot_size);
			//	RDP_DumpRSPCode("unkcode", 0xDEAFF00D, (u32*)(g_pu8RamBase + (((u32)pTask->t.ucode)&0x00FFFFFF)),      0x04001080, 0x1000 - 0x80);//pTask->t.ucode_size);
			break;
	}

	// Started and completed. No need to change cores. [synchronously]
	if( result == PR_COMPLETED )
		RSP_HLE_Finished(SP_STATUS_TASKDONE|SP_STATUS_BROKE|SP_STATUS_HALT);
}
