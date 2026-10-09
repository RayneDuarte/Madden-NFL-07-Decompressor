#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <conio.h>
#include <windows.h>
#include <dirent.h>
#include "ea_madden_decode.h"

#define EA_MADDEN 5

typedef struct
{
	uint32_t offset;
	uint32_t size;
}
_DIR;

typedef struct
{
	uint32_t filetype;
	uint32_t unpacked_size;
}
COMP;

int GetMaxSize(_DIR *DIR1, COMP *InfoSize, int files_num);

int main(int argc, char *argv[])
{
	if (argc < 2 || argc > 2) return 0;
	
	FILE *dat = fopen(argv[1], "rb");
	if (!dat)
	{
		printf("Unable to access the file %s\n", argv[1]);
		return 0;
	}

	fseek(dat, 0, SEEK_END);
	int dat_size = ftell(dat);
	rewind(dat);

	if (dat_size < 16)
	{
		printf("The length of this file is too short\n");
		fclose(dat);
		return 0;
	}

	char magic[5]={0};
	fread(magic, 1, 4, dat);
	if (memcmp(magic, "TERF", 4) != 0)
	{
		printf("Invalid signature for this file.\n");
		printf("Current signature: %s\n", magic);
		printf("Expected: TERF\n");
		fclose(dat);
		return 0;
	}
	
	fseek(dat, 0x0E, SEEK_SET);
	uint16_t files_num;
	fread(&files_num, 2, 1, dat);
	
	_DIR *DIR1 = malloc(sizeof(_DIR)* files_num);
	COMP *UnpackSize = malloc(sizeof(COMP)* files_num);
	
	if (!DIR1 || !UnpackSize)
	{
		printf("Out of memory!\n");
		fclose(dat);
		return 0;
	}

	CreateDirectory("extracted", NULL);
	DIR *dir = opendir("extracted");
	if (!dir)
	{
		printf("Failed to create the output directory for the files\n");
		free(DIR1);
		free(UnpackSize);
		fclose(dat);
		return 0;
	}

	while (ftell(dat) + 16 < dat_size)
	{
		char block[16];
		fread(block, 1, 16, dat);

		if (!memcmp(block, "DIR1", 4))
		{
			int pos = ftell(dat) - 8;
			fseek(dat, pos, SEEK_SET);
		
			int i = 0;
			while (i < files_num)
			{
				fread(&DIR1[i].offset, 4, 1, dat);
				fread(&DIR1[i].size, 4, 1, dat);
				i++;
			}
		}
		else if (!memcmp(block, "COMP", 4))
		{
			int pos = ftell(dat) - 8;
			fseek(dat, pos, SEEK_SET);
		
			int i = 0;
			while (i < files_num)
			{
				fread(&UnpackSize[i].filetype, 4, 1, dat);
				fread(&UnpackSize[i].unpacked_size, 4, 1, dat);
				i++;
			}
		}
		else if (!memcmp(block, "DATA", 4))
		{
			int base_offset = ftell(dat) - 16;		
			int max_filesize = GetMaxSize(DIR1, UnpackSize, files_num);
			unsigned char *cmpdata = malloc(sizeof(unsigned char)* max_filesize);
			unsigned char *decdata = malloc(sizeof(unsigned char)* max_filesize);

			if (!cmpdata || !decdata)
			{
				free(DIR1);
				free(UnpackSize);
				fclose(dat);
				printf("Out of memory!\n");
				return 0;
			}
			
			int i = 0;
			while(i < files_num)
			{
				fseek(dat, DIR1[i].offset + base_offset, SEEK_SET);

				char hexString[12], path[30];
				snprintf(hexString, sizeof(hexString), "0x%08X", i);
				strcpy(path, "extracted\\");
				strcat(path, hexString);

				FILE *out = fopen(path, "wb");
				fread(cmpdata, 1, DIR1[i].size, dat);

				if (UnpackSize[i].filetype == EA_MADDEN)
				{
					int outsz = EA_Madden_Decode(cmpdata, DIR1[i].size, decdata, UnpackSize[i].unpacked_size);
					if (outsz > 0)
						fwrite(decdata, 1, outsz, out);
					else
						printf("The compressed file is damaged or is encoded through Mode 1\n");
				}
				else
					fwrite(cmpdata, 1, DIR1[i].size, out);
				fclose(out);
				
				printf("%s\n", path);
				i++;
			}
		}
	}
	
	free(DIR1);
	free(UnpackSize);
	fclose(dat);
	printf("\nExtracted %d files from %s\n", files_num, argv[1]);
	getch();
}

int GetMaxSize(_DIR *DIR1, COMP *UnpackSize, int files_num)
{
	int maxsize = DIR1[0].size;
	for (int i = 0; i < files_num; i++)
	{
		if (DIR1[i].size > maxsize) maxsize = DIR1[i].size;
	}
	
	for (int i = 0; i < files_num; i++)
	{
		if (UnpackSize[i].unpacked_size > maxsize)
			maxsize = UnpackSize[i].unpacked_size;
	}
	return maxsize;
}

