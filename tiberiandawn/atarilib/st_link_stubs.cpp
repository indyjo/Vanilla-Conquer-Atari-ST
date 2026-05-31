/*
 * Atari ST link stubs for symbols normally provided by WIN32LIB/dllinterface.cpp,
 * MPLAYER.CPP, or the full VQA stack (not yet linked on ST).
 */

#include "function.h"
#include "externs.h"
#include "sidebarglyphx.h"
#include "interpal.h"
#include "gbuffer.h"
#include "vqaconfig.h"
#include "vqatask.h"
#include "vqaloader.h"

#include <stdlib.h>
#include <string.h>

unsigned char* InterpolationPalette = nullptr;
bool InterpolationPaletteChanged = false;

Sample_Type SampleType = SAMPLE_NONE;
SFX_Type SoundType = SFX_NONE;
bool VQPaletteChange = false;

SidebarGlyphxClass* Get_Current_Context_Sidebar(HouseClass* player_ptr)
{
    (void)player_ptr;
    return nullptr;
}

void Read_Scenario_Descriptions(void)
{
    MPlayerScenarios.Clear();
    MPlayerFilenum.Clear();
}

void Free_Scenario_Descriptions(void)
{
    MPlayerScenarios.Clear();
    MPlayerFilenum.Clear();
}

void Read_MultiPlayer_Settings(void)
{
}

void Write_MultiPlayer_Settings(void)
{
}

GameType Select_MPlayer_Game(void)
{
    return GAME_NORMAL;
}

void Computer_Message(void)
{
}

int Surrender_Dialog(void)
{
    return 0;
}

bool ConnectionLost = false;
bool GameStatisticsPacketSent = false;

void VQA_DefaultConfig(VQAConfig* config)
{
    if (config) {
        memset(config, 0, sizeof(*config));
    }
}

VQAHandle* VQA_Alloc(void)
{
    VQAHandle* handle = (VQAHandle*)malloc(sizeof(VQAHandle));
    if (handle) {
        memset(handle, 0, sizeof(*handle));
    }
    return handle;
}

void VQA_Free(void* block)
{
    free(block);
}

void VQA_Init(VQAHandle* handle, StreamHandlerFuncPtr streamhandler)
{
    if (handle) {
        handle->StreamHandler = streamhandler;
    }
}

int VQA_Open(VQAHandle* handle, char const* filename, VQAConfig* config)
{
    (void)handle;
    (void)filename;
    (void)config;
    return -1;
}

void VQA_Close(VQAHandle* handle)
{
    (void)handle;
}

VQAErrorType VQA_Play(VQAHandle* handle, VQAPlayMode mode)
{
    (void)handle;
    (void)mode;
    return VQAERR_NONE;
}

void VQA_PauseAudio(void)
{
}

void VQA_ResumeAudio(void)
{
}

void Read_Interpolation_Palette(char const* palette_file_name)
{
    (void)palette_file_name;
}

void Write_Interpolation_Palette(char const* palette_file_name)
{
    (void)palette_file_name;
}

void Create_Palette_Interpolation_Table(void)
{
}

void Increase_Palette_Luminance(unsigned char* interpolation_palette,
    int red_percentage,
    int green_percentage,
    int blue_percentage,
    int cap)
{
    (void)interpolation_palette;
    (void)red_percentage;
    (void)green_percentage;
    (void)blue_percentage;
    (void)cap;
}

void Interpolate_2X_Scale(GraphicBufferClass* source,
    GraphicViewPortClass* dest,
    char const* palette_file_name,
    int mode)
{
    (void)palette_file_name;
    (void)mode;
    if (source && dest) {
        dest->Blit(*source, TRUE);
    }
}

void Check_VQ_Palette_Set(void)
{
}
