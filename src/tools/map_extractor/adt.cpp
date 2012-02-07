/*
 * Copyright (C) 2010-2012 Oregon <http://www.oregoncore.com/>
 * Copyright (C) 2008-2012 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2012 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define _CRT_SECURE_NO_DEPRECATE

#ifdef WIN32
#include <windows.h>
#endif

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <string>
#include <map>
#include <vector>
#include <set>

#include "adt.h"
#include "mpq_libmpq04.h"

extern uint16 *areas;
extern uint16 *LiqType;
extern uint32 maxAreaId;

vec wmoc;

Cell *cell;
mcell *mcells;
int holetab_h[4] = {0x1111, 0x2222, 0x4444, 0x8888};
int holetab_v[4] = {0x000F, 0x00F0, 0x0F00, 0xF000};

bool LoadADT(char* filename)
{
    size_t size;
    MPQFile mf(filename);

    if (mf.isEof())
    {
        //printf("No such file %s\n", filename);
        return false;
    }

    MapLiqFlag = new uint8[256];
    for (uint32 j = 0; j < 256; ++j)
        MapLiqFlag[j] = 0;                                  // no water

    MapLiqHeight = new float[16384];
    for (uint32 j = 0; j < 16384; ++j)
        MapLiqHeight[j] = -999999;                          // no water

    mcells = new mcell;

    wmoc.x = 65 * TILESIZE;
    wmoc.z = 65 * TILESIZE;

    size_t mcnk_offsets[256], mcnk_sizes[256];

    chunk_num = 0;
    k = 0;
    m = 0;
    while (!mf.isEof())
    {
        uint32 fourcc;
        mf.read(&fourcc, 4);
        mf.read(&size, 4);

        size_t nextpos = mf.getPos() + size;

        //if (fourcc==0x4d484452)                            // MHDR header
        //if (fourcc==0x4d564552)                            // MVER
        if (fourcc == 0x4d43494e)                            // MCIN
        {
            for (uint32 i = 0; i < 256; ++i)
            {
                mf.read(&mcnk_offsets[i], 4);
                mf.read(&mcnk_sizes[i], 4);
                mf.seekRelative(8);
            }
        }
        //if (fourcc == 0x4d544558)                          // MTEX textures (strings)
        //if (fourcc == 0x4d4d4458)                          // MMDX m2 models (strings)
        //if (fourcc == 0x4d4d4944)                          // MMID offsets for strings in MMDX
        //if (fourcc == 0x4d574d4f)                          // MWMO
        //if (fourcc == 0x4d574944)                          // MWID offsets for strings in MWMO
        //if (fourcc == 0x4d444446)                          // MDDF
        //if (fourcc == 0x4d4f4446)                          // MODF
        //if (fourcc == 0x4d48324f)                          // MH2O new in WotLK
        //if (fourcc == 0x4d46424f)                          // MFBO new in BC
        //if (fourcc == 0x4d434e4b)                          // MCNK
        //if (fourcc == 0x4d564552)                          // MVER
        //if (fourcc == 0x4d484452)                          // MHDR header

        mf.seek(nextpos);
    }

    //printf("Loading chunks info\n");
    // read individual map chunks
    chunk_num = 0;
    k = 0;
    m = 0;
    for (int j = 0; j < 16; ++j)
    {
        for (int i = 0; i < 16; ++i)
        {
            mf.seek((int)mcnk_offsets[j * 16 + i]);
            LoadMapChunk(mf, &(mcells->ch[i][j]));
            ++chunk_num;
        }
    }
    mf.close();
    return true;
}

bool isHole(int holes, int i, int j)
{
    int testi = i / 2;
    int testj = j / 4;
    if (testi > 3) testi = 3;
    if (testj > 3) testj = 3;
    return (holes & holetab_h[testi] & holetab_v[testj]) != 0;
}

inline void LoadMapChunk(MPQFile &mf, chunk *_chunk)
{
    float h;
    uint32 fourcc;
    uint32 size;
    MapChunkHeader header;

    mf.seekRelative(4);
    mf.read(&size, 4);

    size_t lastpos = mf.getPos() + size;
    mf.read(&header, 0x80);
    _chunk->area_id = header.areaid;

    float xbase = header.xpos;
    float ybase = header.ypos;
    float zbase = header.zpos;
    zbase = TILESIZE * 32 - zbase;
    xbase = TILESIZE * 32 - xbase;
    if (wmoc.x > xbase) wmoc.x = xbase;
    if (wmoc.z > zbase) wmoc.z = zbase;
    int chunkflags = header.flags;
    float zmin = 999999999.0f;
    float zmax = -999999999.0f;
    // must be there, bl!zz uses some crazy format
    while (mf.getPos() < lastpos)
    {
        mf.read(&fourcc, 4);
        mf.read(&size, 4);
        size_t nextpos = mf.getPos() + size;
        if (fourcc == 0x4d435654)                            // MCVT
        {
            for (int j = 0; j < 17; ++j)
            {
                for (int i = 0; i < ((j % 2) ? 8 : 9); ++i)
                {
                    mf.read(&h, 4);
                    float z = h + ybase;
                    if (j % 2)
                    {
                        if (isHole(header.holes, i, j))
                            _chunk->v8[i][j / 2] = -1000;
                        else
                            _chunk->v8[i][j / 2] = z;
                    }
                    else
                    {
                        if (isHole(header.holes, i, j))
                            _chunk->v9[i][j / 2] = -1000;
                        else
                            _chunk->v9[i][j / 2] = z;
                    }

                    if (z > zmax) zmax = z;
                    //if (z < zmin) zmin = z;
                }
            }
        }
        else if (fourcc == 0x4d434e52)                       // MCNR
        {
            nextpos = mf.getPos() + 0x1C0;                  // size fix
        }
        else if (fourcc == 0x4d434c51)                       // MCLQ
        {
            // liquid / water level
            char fcc1[5];
            mf.read(fcc1, 4);
            flipcc(fcc1);
            fcc1[4] = 0;
            float *ChunkLiqHeight = new float[81];

            if (!strcmp(fcc1, "MCSE"))
            {
                for (int j = 0; j < 81; ++j)
                {
                    ChunkLiqHeight[j] = -999999;            // no liquid/water
                }
            }
            else
            {
                float maxheight;
                mf.read(&maxheight, 4);
                for (int j = 0; j < 81; ++j)
                {
                    LiqData liq;
                    mf.read(&liq, 8);

                    if (liq.height > maxheight)
                        ChunkLiqHeight[j] = -999999;
                    else
                        ChunkLiqHeight[j] = h;
                }

                if (chunkflags & 4 || chunkflags & 8)
                    MapLiqFlag[chunk_num] |= 1;             // water
                if (chunkflags & 16)
                    MapLiqFlag[chunk_num] |= 2;             // magma/slime
            }
            if (!(chunk_num % 16))
                m = 1024 * (chunk_num / 16);
            k = m + (chunk_num % 16) * 8;

            for (int p = 0; p < 72; p += 9)
            {
                for (int s = 0; s < 8; ++s)
                {
                    MapLiqHeight[k] = ChunkLiqHeight[p + s];
                    ++k;
                }
                k = k + 120;
            }
            delete []ChunkLiqHeight;
            break;
        }
        mf.seek(nextpos);
    }
}

inline void TransformData()
{
    cell = new Cell;

    for (uint32 x = 0; x < 128; ++x)
    {
        for (uint32 y = 0; y < 128; ++y)
        {
            cell->v8[y][x] = (float)mcells->ch[x / 8][y / 8].v8[x % 8][y % 8];
            cell->v9[y][x] = (float)mcells->ch[x / 8][y / 8].v9[x % 8][y % 8];
        }

        // extra 1 point on bounds
        cell->v9[128][x] = (float)mcells->ch[x / 8][15].v9[x % 8][8];
        // x == y
        cell->v9[x][128] = (float)mcells->ch[15][x / 8].v9[8][x % 8];
    }

    // and the last 1
    cell->v9[128][128] = (float)mcells->ch[15][15].v9[8][8];

    delete mcells;
}

const char MAP_MAGIC[] = "MAP_2.50";                        // for destination from 3.0.8a maps

bool ConvertADT(char *filename, char *filename2)
{
    if (!LoadADT(filename))
        return false;

    FILE *output=fopen(filename2, "wb");
    if (!output)
    {
        printf("Can't create the output file '%s'\n", filename2);
        delete [] MapLiqHeight;
        delete [] MapLiqFlag;
        return false;
    }

    // write magic header
    fwrite(MAP_MAGIC, 1, 8, output);

    for (uint32 x = 0; x < 16; ++x)
    {
        for (uint32 y = 0; y < 16; ++y)
        {
            if (mcells->ch[y][x].area_id && mcells->ch[y][x].area_id <= maxAreaId)
            {
                if (areas[mcells->ch[y][x].area_id] == 0xffff)
                    printf("\nCan't find area flag for areaid %u.\n", mcells->ch[y][x].area_id);

                fwrite(&areas[mcells->ch[y][x].area_id], 1, 2, output);
            }
            else
            {
                uint16 flag = 0xffff;
                fwrite(&flag, 1, 2, output);
            }
        }
    }

    fwrite(MapLiqFlag, 1, 256, output);
    delete [] MapLiqFlag;

    fwrite(MapLiqHeight, sizeof(float), 16384, output);
    delete [] MapLiqHeight;

    TransformData();

    fwrite(&cell->v9, 1, sizeof(cell->v9), output);
    fwrite(&cell->v8, 1, sizeof(cell->v8), output);
    fclose(output);
    delete cell;

    return true;
}

