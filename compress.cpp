/*

Copyright (c) 2015 Harm Hanemaaijer <fgenfb@yahoo.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

#include <sched.h>
#include <dstRandom.h>
#include "detex.h"
#include "compress.h"
#include "compress-block.h"

// #define VERBOSE

static DETEX_INLINE_ONLY void AddErrorPixelXYRGBX8(const uint8_t * DETEX_RESTRICT pix1,
const uint8_t * DETEX_RESTRICT pix2, int pix2_stride, int dx, int dy, uint32_t & DETEX_RESTRICT error) {
	uint32_t pixel1 = *(uint32_t *)(pix1 + (dy * 4 + dx) * 4);
	int r1 = detexPixel32GetR8(pixel1);
	int g1 = detexPixel32GetG8(pixel1);
	int b1 = detexPixel32GetB8(pixel1);
	uint32_t pixel2 = *(uint32_t *)(pix2 + dy * pix2_stride + dx * 4);
	int r2 = detexPixel32GetR8(pixel2);
	int g2 = detexPixel32GetG8(pixel2);
	int b2 = detexPixel32GetB8(pixel2);
	error += GetPixelErrorRGB8(r1, g1, b1, r2, g2, b2);
}

static uint32_t detexCalculateErrorRGBX8(const detexTexture * DETEX_RESTRICT texture, int x, int y,
uint8_t * DETEX_RESTRICT pixel_buffer) {
	uint8_t *pix1 = pixel_buffer;
	uint8_t *pix2 = texture->data + (y * texture->width + x) * 4;
	int pix2_stride = texture->width * 4;
	uint32_t error = 0;
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 0, 0, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 1, 0, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 2, 0, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 3, 0, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 0, 1, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 1, 1, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 2, 1, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 3, 1, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 0, 2, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 1, 2, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 2, 2, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 3, 2, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 0, 3, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 1, 3, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 2, 3, error);
	AddErrorPixelXYRGBX8(pix1, pix2, pix2_stride, 3, 3, error);
	return error;
}

static const detexCompressionInfo compression_info[] = {
	{ 2, DETEX_ERROR_UNIT_UINT32, detexGetModeBC1, detexSetModeBC1,
	MutateBC1, SetPixelsBC1, detexCalculateErrorRGBX8 },
};

static double detexCompressBlock(const detexCompressionInfo * DETEX_RESTRICT info,
const detexTexture * DETEX_RESTRICT texture, int x, int y, int mode, dstCMWCRNG *rng,
uint8_t * DETEX_RESTRICT bitstring_out, uint32_t output_format) {
	uint8_t bitstring[16];
//	uint8_t pixel_buffer[DETEX_MAX_BLOCK_SIZE];
	int compressed_block_size = detexGetCompressedBlockSize(output_format);
	uint32_t best_error_uint32 = UINT_MAX;
	uint64_t best_error_uint64 = UINT64_MAX;
	double best_error_double = DBL_MAX;
	int last_improvement_generation = -1;
	for (int generation = 0; generation < 2048 || last_improvement_generation > generation - 384;) {
		if (generation < 256) {
			// For the first 256 iterations, generate a random bitstring.
			uint32_t *bitstring32 = (uint32_t *)bitstring;
			*(uint32_t *)bitstring32 = rng->Random32();
			*(uint32_t *)(bitstring + 4) = rng->Random32();
			if (compressed_block_size == 16) {
				*(uint32_t *)(bitstring + 8) = rng->Random32();
				*(uint32_t *)(bitstring + 12) = rng->Random32();
			}
			if (mode >= 0)
				info->set_mode_func(bitstring, mode, 0, NULL);
		}
		else {
			// From iteration 256, use mutation.
			memcpy(bitstring, bitstring_out, compressed_block_size);
			info->mutate_func(rng, generation, mode, bitstring);
		}
		uint32_t error_uint32;
		uint64_t error_uint64;
		double error_double;
		bool is_better;
		if (info->error_unit == DETEX_ERROR_UNIT_UINT32) {
//			if (mode >= 0 && info->get_mode_func(bitstring) != mode)
//				error_uint32 = UINT_MAX;
//			else
				error_uint32 = info->set_pixels_error_uint32_func(texture, x, y, bitstring);
			is_better = (error_uint32 < best_error_uint32);
			if (is_better)
				best_error_uint32 = error_uint32;
#ifdef VERBOSE
			if ((generation & 127) == 0)
				printf("Gen %d: RMSE = %.3f\n", generation,
					sqrt((double)best_error_uint32 / 16.0d));
#endif
		}
		else if (info->error_unit == DETEX_ERROR_UNIT_UINT64) {
			error_uint64 = info->set_pixels_error_uint64_func(texture, x, y, bitstring);

			is_better = (error_uint64 < best_error_uint64);
			if (is_better)
				best_error_uint64 = error_uint64;
		}
		else if (info->error_unit == DETEX_ERROR_UNIT_DOUBLE) {
			error_double = info->set_pixels_error_double_func(texture, x, y, bitstring);
			is_better = (error_double < best_error_double);
			if (is_better)
				best_error_double = error_double;
		}
		if (is_better) {
			memcpy(bitstring_out, bitstring, compressed_block_size);
			last_improvement_generation = generation;
		}
		generation++;
	}
	double rmse = sqrt((double)best_error_uint32 / 16.0d);
#ifdef VERBOSE
	printf("Block RMSE (mode %d): %.3f\n", mode, rmse);
#endif
	return rmse;
}

struct ThreadData {
	const detexTexture *texture;
	uint8_t *pixel_buffer;
	uint32_t output_format;
	int x_start;
	int y_start;
	int x_end;
	int y_end;
	int nu_tries;
	bool modal;
	dstCMWCRNG *rng;
};

static void *CompressBlocksThread(void *_thread_data) {
	ThreadData *thread_data = (ThreadData *)_thread_data;
	const detexTexture *texture = thread_data->texture;
	uint8_t *pixel_buffer = thread_data->pixel_buffer;
	int compressed_format_index = detexGetCompressedFormat(thread_data->output_format);
	int block_size = detexGetCompressedBlockSize(thread_data->output_format);
	for (int y = thread_data->y_start; y < thread_data->y_end; y += 4)
		for (int x = 0; x < thread_data->x_end; x += 4) {
			// Calculate the block index.
			int i = (y / 4) * (texture->width / 4) + x / 4;
			double best_rmse = DBL_MAX;
			for (int j = 0; j < thread_data->nu_tries; j++) {
				uint8_t bitstring[16];
				if (thread_data->modal)
					// Compress the block using each mode.
					for (int mode = 0; mode < compression_info[compressed_format_index
					- 1].nu_modes; mode++) {
						double rmse = detexCompressBlock(
							&compression_info[compressed_format_index - 1],
							texture, x, y, mode, thread_data->rng,
							bitstring, thread_data->output_format);
						if (rmse < best_rmse) {
							best_rmse = rmse;
							memcpy(&pixel_buffer[i * block_size], bitstring, block_size);
						}
					}
				else {
					double rmse = detexCompressBlock(
						&compression_info[compressed_format_index - 1],
						texture, x, y, -1, thread_data->rng,
						bitstring, thread_data->output_format);
					if (rmse < best_rmse) {
						best_rmse = rmse;
						memcpy(&pixel_buffer[i * block_size], bitstring, block_size);
					}
				}
			}
		}
	return NULL;
}

bool detexCompressTexture(int nu_tries, bool modal, const detexTexture * DETEX_RESTRICT texture,
uint8_t * DETEX_RESTRICT pixel_buffer, uint32_t output_format) {
	// Calculate the number of blocks.
	int nu_blocks = (texture->height / 4) * (texture->width / 4);
	int nu_threads = sysconf(_SC_NPROCESSORS_CONF);
	int nu_blocks_per_thread = nu_blocks / nu_threads;
	if (nu_blocks_per_thread < 32) {
		nu_threads = nu_blocks / 32;
		if (nu_threads == 0)
			nu_threads = 1;
	}
	pthread_t *thread = (pthread_t *)malloc(sizeof(pthread_t) * nu_threads);
	ThreadData *thread_data = (ThreadData *)malloc(sizeof(ThreadData) * nu_threads);
	for (int i = 0; i < nu_threads; i++) {
		thread_data[i].texture = texture;
		thread_data[i].pixel_buffer = pixel_buffer;
		thread_data[i].output_format = output_format;
		thread_data[i].x_start = 0;
		thread_data[i].y_start = i * texture->height / nu_threads;
		thread_data[i].x_end = texture->width;
		thread_data[i].y_end = (i + 1) * texture->height / nu_threads;
		thread_data[i].nu_tries = nu_tries;
		thread_data[i].modal = modal;
		thread_data[i].rng = new dstCMWCRNG;
		if (i < nu_threads - 1)
			pthread_create(&thread[i], NULL, CompressBlocksThread, &thread_data[i]);
		else
			CompressBlocksThread(&thread_data[i]);			
	}
	for (int i = 0; i < nu_threads - 1; i++)
		pthread_join(thread[i], NULL);
	for (int i = 0; i < nu_threads; i++)
		delete thread_data[i].rng;
	free(thread_data);
	free(thread);
	return true;
}

// Compare and return RMSE.
double detexCompareTextures(const detexTexture * DETEX_RESTRICT input_texture,
detexTexture * DETEX_RESTRICT compressed_texture, double *average_rmse, double *rmse_sd) {
	uint8_t pixel_buffer[DETEX_MAX_BLOCK_SIZE];
	int compressed_format_index = detexGetCompressedFormat(compressed_texture->format);
	const detexCompressionInfo *info = &compression_info[compressed_format_index - 1];
	int block_size = detexGetCompressedBlockSize(compressed_texture->format);
	double total_error = 0;
	int nu_blocks = compressed_texture->width_in_blocks * compressed_texture->height_in_blocks;
	double *block_rmse = (double *)malloc(sizeof(double) * nu_blocks);
	for (int y = 0; y < compressed_texture->height; y += 4)
		for (int x = 0; x < compressed_texture->width; x += 4) {
			// Calculate the block index.
			int i = (y / 4) * (compressed_texture->width / 4) + x / 4;
			// Decompress block.
			bool r = detexDecompressBlock(&compressed_texture->data[i * block_size],
				compressed_texture->format, DETEX_MODE_MASK_ALL,
				DETEX_DECOMPRESS_FLAG_ENCODE, pixel_buffer,
				detexGetPixelFormat(compressed_texture->format));
			if (!r) {
				printf("Error during decompression for final image comparison\n");
				exit(1);
			}
			if (info->error_unit == DETEX_ERROR_UNIT_UINT32) {
				uint32_t error = info->calculate_error_uint32_func(input_texture, x, y,
					pixel_buffer);
				total_error += error;
				block_rmse[i] = sqrt(error / 16);
			}
			else if (info->error_unit == DETEX_ERROR_UNIT_UINT64)
				total_error += info->calculate_error_uint64_func(input_texture, x, y,
					pixel_buffer);
			else if (info->error_unit == DETEX_ERROR_UNIT_DOUBLE)
				total_error += info->calculate_error_double_func(input_texture, x, y,
					pixel_buffer);
		}
	int nu_pixels = compressed_texture->width * compressed_texture->height;
	double rmse = sqrt(total_error / nu_pixels);
	if (average_rmse != NULL || rmse_sd != NULL) {
		double average = 0.0d;
		for (int i = 0; i < nu_blocks; i++)
			average += block_rmse[i];
		average /= nu_blocks;
		if (average_rmse != NULL)
			*average_rmse = average;
		if (rmse_sd != NULL) {
			double variance = 0.0d;
			for (int i = 0; i < nu_blocks; i++)
				variance += (block_rmse[i] - average) * (block_rmse[i] - average);
			variance /= nu_blocks;
			if (rmse_sd != NULL)
				*rmse_sd = sqrt(variance);
		}
	}
	free(block_rmse);
	return rmse;
}
