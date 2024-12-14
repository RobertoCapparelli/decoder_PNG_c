#include "zlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>


//todo: apply check for index- color, parse chunk PLTE.
//get color palette and apply to filter!
typedef struct PNG_decoder
{
    unsigned char *data;
    size_t data_size;
    size_t offset;
    unsigned char *idat_data;
    char **texts;
    unsigned int width;
    unsigned int height;
    size_t idat_size;
    size_t text_count;
    unsigned char bit_depth;
    unsigned char color_type;
    unsigned char compression_method;
    unsigned char filter_method;
    unsigned char interlace_method;
} PNG_decoder_t;

#pragma region Declarations
int initialize_decoder(PNG_decoder_t *decoder, const char *filename);

void parse_chunks(PNG_decoder_t *decoder);
void parse_IHDR(PNG_decoder_t *decoder, unsigned char *chunk_data);
void parse_IDAT(PNG_decoder_t *decoder, unsigned char *chunk_data, size_t chunk_size);
void parse_tEXt(PNG_decoder_t *decoder, unsigned char *chunk_data, size_t chunk_size);
uint32_t to_big_endian(uint8_t *bytes);
void print_PNG_info(PNG_decoder_t *decoder);
int decompress_IDAT(PNG_decoder_t *decoder, unsigned char **out_data, size_t *out_size);
unsigned char *apply_filters(PNG_decoder_t *decoder, unsigned char *decompressed_data);
void no_filter(unsigned char *output, unsigned char *scanline, size_t bytes_per_pixel, size_t width);
void sub_filter(unsigned char *output, unsigned char *scanline, size_t bytes_per_pixel, size_t width);
void up_filter(unsigned char *output, unsigned char *scanline, unsigned char *prev_scanline, size_t bytes_per_pixel, size_t width);
void average_filter(unsigned char *output, unsigned char *scanline, unsigned char *prev_scanline, size_t bytes_per_pixel, size_t width);
unsigned char paeth_predictor(unsigned char left, unsigned char up, unsigned char upper_left);
void paeth_filter(unsigned char *output, unsigned char *scanline, unsigned char *prev_scanline, size_t bytes_per_pixel, size_t width);

#pragma endregion

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <filename.png>\n", argv[0]);
        return EXIT_FAILURE;
    }
    const char *filename = argv[1];

    PNG_decoder_t decoder;
    if (initialize_decoder(&decoder, filename) != 0)
    {
        fprintf(stderr, "Failed to initialize PNG decoder.\n");
        return EXIT_FAILURE;
    }

    parse_chunks(&decoder);

    // INFO
    print_PNG_info(&decoder);
    printf("\nText Data:\n");
    for (size_t i = 0; i < decoder.text_count; i++)
    {
        printf("Text[%zu]: %s\n", i, decoder.texts[i]);
    }
    printf("\nIDAT Data Size: %zu bytes\n", decoder.idat_size);

    // Decompression
    unsigned char *decompressed_data = NULL;
    size_t decompressed_size = 0;

    if (decompress_IDAT(&decoder, &decompressed_data, &decompressed_size) != 0)
    {
        fprintf(stderr, "Failed to decompress IDAT data.\n");
        free(decoder.data);
        free(decoder.idat_data);
        for (size_t i = 0; i < decoder.text_count; i++)
        {
            free(decoder.texts[i]);
        }
        free(decoder.texts);
        return EXIT_FAILURE;
    }

    printf("\nDecompressed Data Size: %zu bytes\n", decompressed_size);

    // Apply filters
    unsigned char *filtered_data = apply_filters(&decoder, decompressed_data);
    if (!filtered_data)
    {
        fprintf(stderr, "Failed to apply filters.\n");
        // free(decompressed_data);
        return EXIT_FAILURE;
    }

    // FREE
    free(decompressed_data);
    free(decoder.data);
    free(decoder.idat_data);
    for (size_t i = 0; i < decoder.text_count; i++)
    {
        free(decoder.texts[i]);
    }
    free(decoder.texts);

    return EXIT_SUCCESS;
}

#pragma region Definitions
int initialize_decoder(PNG_decoder_t *decoder, const char *filename)
{
    FILE *file;
    if (fopen_s(&file, filename, "rb") != 0)
    {
        perror("Failed to open file");
        return -1;
    }

    fseek(file, 0, SEEK_END);         // pointer at the end of the file for the size
    decoder->data_size = ftell(file); // get size
    if (decoder->data_size == -1)
    {
        perror("Failed to determine file size");
        fclose(file);
        return -1;
    }
    fseek(file, 0, SEEK_SET); // pointer at the start of the file

    // Read Data
    decoder->data = (unsigned char *)malloc(decoder->data_size);
    if (!decoder->data)
    {
        fclose(file);
        return -1;
    }
    fread(decoder->data, 1, decoder->data_size, file);
    // Read the entire file into memory for efficient processing of PNG chunks
    // This approach is suitable because PNG files are typically small.
    fclose(file);

    // PNG signature "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"
    if (memcmp(decoder->data, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) != 0)
    {
        fprintf(stderr, "Invalid PNG file!\n");
        free(decoder->data);
        return -1;
    }

    decoder->offset = 8; // Skip PNG signature "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A"
    decoder->idat_data = NULL;
    decoder->idat_size = 0;
    decoder->texts = NULL;
    decoder->text_count = 0;

    return 0;
}
void parse_chunks(PNG_decoder_t *decoder)
{
    while (decoder->offset < decoder->data_size)
    {
        // GOTO line 139 for explanation
        uint32_t chunk_size = to_big_endian(decoder->data + decoder->offset);
        decoder->offset += 4;

        // chunk type
        unsigned char *chunk_type = decoder->data + decoder->offset;
        decoder->offset += 4;

        // chunck data
        unsigned char *chunk_data = decoder->data + decoder->offset;
        decoder->offset += chunk_size;

        // CRC
        decoder->offset += 4;

        if (memcmp(chunk_type, "IHDR", 4) == 0)
        {
            parse_IHDR(decoder, chunk_data);
        }
        else if (memcmp(chunk_type, "IDAT", 4) == 0)
        {
            parse_IDAT(decoder, chunk_data, chunk_size);
        }
        else if (memcmp(chunk_type, "tEXt", 4) == 0)
        {
            parse_tEXt(decoder, chunk_data, chunk_size);
        }
        else if (memcmp(chunk_type, "IEND", 4) == 0)
        {
            break;
        }
        else
        {
            fprintf(stderr, "Unknown chunk type: %.4s\n", chunk_type);
        }
    }
}
#pragma region parse_chunk
// The PNG file stores width and height as 4-byte integers in Big-Endian format.
// This code reconstructs the 32-bit value by shifting each byte to its correct position:
// chunk_data[0] << 24 (most significant byte)
// chunk_data[1] << 16
// chunk_data[2] << 8
// chunk_data[3] (least significant byte)
// The `|` operator combines them into a single 32-bit integer.

/*   decoder->width = (chunk_data[0] << 24) | (chunk_data[1] << 16) |
                    (chunk_data[2] << 8) | chunk_data[3];
   decoder->height = (chunk_data[4] << 24) | (chunk_data[5] << 16) |
                     (chunk_data[6] << 8) | chunk_data[7]; */

// Not a good implement, use memcpy!        GOTO line 185

void parse_IHDR(PNG_decoder_t *decoder, unsigned char *chunk_data)
{
    // decoder->width = to_big_endian(chunk_data);
    uint8_t big_endian_data_width[4] = {chunk_data[0], chunk_data[1], chunk_data[2], chunk_data[3]};
    decoder->width = to_big_endian(big_endian_data_width);

    // decoder->height = to_big_endian(chunk_data + 4);
    uint8_t big_endian_data_height[4] = {chunk_data[4], chunk_data[5], chunk_data[6], chunk_data[7]};
    decoder->height = to_big_endian(big_endian_data_height);

    decoder->bit_depth = chunk_data[8];
    decoder->color_type = chunk_data[9];
    decoder->compression_method = chunk_data[10];
    decoder->filter_method = chunk_data[11];
    decoder->interlace_method = chunk_data[12];
}

void parse_IDAT(PNG_decoder_t *decoder, unsigned char *chunk_data, size_t chunk_size)
{
    decoder->idat_data = (unsigned char *)realloc(decoder->idat_data, decoder->idat_size + chunk_size);
    memcpy(decoder->idat_data + decoder->idat_size, chunk_data, chunk_size);
    decoder->idat_size += chunk_size;
}

void parse_tEXt(PNG_decoder_t *decoder, unsigned char *chunk_data, size_t chunk_size)
{
    decoder->texts = (char **)realloc(decoder->texts, (decoder->text_count + 1) * sizeof(char *));
    decoder->texts[decoder->text_count] = (char *)malloc(chunk_size + 1);
    memcpy(decoder->texts[decoder->text_count], chunk_data, chunk_size);
    decoder->texts[decoder->text_count][chunk_size] = '\0';
    decoder->text_count++;
}
int decompress_IDAT(PNG_decoder_t *decoder, unsigned char **out_data, size_t *out_size)
{

    // STANDARD METHOD FROM .zlib IN C FOR DECOMPRESSION

    z_stream stream; // struct of zlib for compress/decompress data
    memset(&stream, 0, sizeof(stream));

    if (inflateInit(&stream) != Z_OK)
    {
        fprintf(stderr, "Failed to initialize zlib for decompression.\n");
        return -1;
    }

    // Imposta l'input (dati IDAT compressi)
    stream.next_in = decoder->idat_data;  // stream.next_in: Pointer data to decompress
    stream.avail_in = decoder->idat_size; // stream.avail_in: data size

    // buffer for decompressed data
    size_t buffer_size = decoder->width * decoder->height * 4 + decoder->height; // + decoder->height: for add filter
    *out_data = (unsigned char *)malloc(buffer_size);
    if (!(*out_data))
    {
        fprintf(stderr, "Failed to allocate memory for decompressed data.\n");
        inflateEnd(&stream);
        return -1;
    }

    stream.next_out = *out_data;    // pointer to buffer for decompressed data (not yet decompressed)
    stream.avail_out = buffer_size; // data size

    // Decompress data
    int ret = inflate(&stream, Z_FINISH); // inflate: decompress data from stream.next_in to stream.next_out
    if (ret != Z_STREAM_END)
    {
        fprintf(stderr, "Failed to decompress IDAT data: %d\n", ret);
        // free(*out_data);
        inflateEnd(&stream);
        return -1;
    }

    *out_size = stream.total_out;

    inflateEnd(&stream);

    return 0;
}

#pragma endregion

#pragma region Filters
void no_filter(unsigned char *output, unsigned char *scanline, size_t bytes_per_pixel, size_t width)
{
    memcpy(output, scanline, width * bytes_per_pixel);
}
void sub_filter(unsigned char *output, unsigned char *scanline, size_t bytes_per_pixel, size_t width)
{
    for (size_t x = 0; x < width; x++)
    {
        for (size_t y = 0; y < bytes_per_pixel; y++)
        {
            unsigned char left = (x == 0) ? 0 : output[(x - 1) * bytes_per_pixel + y];
            output[x * bytes_per_pixel + y] = scanline[x * bytes_per_pixel + y] + left;
        }
    }
}
void up_filter(unsigned char *output, unsigned char *scanline, unsigned char *prev_scanline, size_t bytes_per_pixel, size_t width)
{
    for (size_t x = 0; x < width * bytes_per_pixel; x++)
    {
        unsigned char up = (prev_scanline == NULL) ? 0 : prev_scanline[x];
        output[x] = scanline[x] + up;
    }
}
void average_filter(unsigned char *output, unsigned char *scanline, unsigned char *prev_scanline, size_t bytes_per_pixel, size_t width)
{
    // reconstructed = raw + ((left + up) / 2)

    for (size_t x = 0; x < width; x++)
    {
        for (size_t y = 0; y < bytes_per_pixel; y++)
        {
            unsigned char left = (x == 0) ? 0 : output[(x - 1) * bytes_per_pixel + y];
            unsigned char up = (prev_scanline == NULL) ? 0 : prev_scanline[x * bytes_per_pixel + y];

            output[x * bytes_per_pixel + y] = scanline[x * bytes_per_pixel + y] + ((left + up) / 2);
        }
    }
}
unsigned char paeth_predictor(unsigned char left, unsigned char up, unsigned char upper_left)
{
    // The prediction minimizes the sum of absolute differences between the actual value and the three neighbors.

    int p = left + up - upper_left;
    int pa = abs(p - left);
    int pb = abs(p - up);
    int pc = abs(p - upper_left);

    // Return the neighbor (left, up, or upper_left) with the smallest difference (pa, pb, pc).
    if (pa <= pb && pa <= pc)
        return left;
    else if (pb <= pc)
        return up;
    else
        return upper_left;
}
void paeth_filter(unsigned char *output, unsigned char *scanline, unsigned char *prev_scanline, size_t bytes_per_pixel, size_t width)
{
    // reconstructed = raw + predicted

    for (size_t x = 0; x < width; x++)
    {
        for (size_t y = 0; y < bytes_per_pixel; y++)
        {
            unsigned char left = (x == 0) ? 0 : output[(x - 1) * bytes_per_pixel + y];
            unsigned char up = (prev_scanline == NULL) ? 0 : prev_scanline[x * bytes_per_pixel + y];
            unsigned char upper_left = (x == 0 || prev_scanline == NULL) ? 0 : prev_scanline[(x - 1) * bytes_per_pixel + y];

            unsigned char predicted = paeth_predictor(left, up, upper_left);
            output[x * bytes_per_pixel + y] = scanline[x * bytes_per_pixel + y] + predicted;
        }
    }
}

unsigned char *apply_filters(PNG_decoder_t *decoder, unsigned char *decompressed_data)
{
    /*Calculate the number of bytes per pixel based on the bit depth and color type.
        decoder->bit_depth / 8: Converts bit depth (8 or 16 bits per channel) into bytes per channel.
        decoder->color_type == 2: Checks if the color type is Truecolor (RGB, 3 channels).
       If Truecolor, multiplies by 3 to account for red, green, and blue channels, if 1 channel multiplies to 1.

     TODO: Limitations
     This calculation assumes only Truecolor (RGB) and single-channel formats.
     It does not handle other valid PNG color types, such as:
       - Truecolor with alpha (RGBA, 4 channels)
       - Grayscale with alpha (2 channels) */
    size_t bytes_per_pixel = (decoder->bit_depth / 8) * ((decoder->color_type == 2) ? 3 : 1);

    size_t scanline_size = decoder->width * bytes_per_pixel + 1;

    unsigned char *output = (unsigned char *)malloc(decoder->width * decoder->height * bytes_per_pixel);
    if (!output)
    {
        fprintf(stderr, "Failed to allocate memory for filtered image.\n");
        return NULL;
    }

    unsigned char *prev_scanline = NULL;
    unsigned char *current_output = output;

    size_t filter_counts[5] = {0};

    for (size_t y = 0; y < decoder->height; y++)
    {
        unsigned char filter_type = decompressed_data[y * scanline_size];
        if (filter_type > 4)
        {
            fprintf(stderr, "Invalid filter type: %u at scanline %zu\n", filter_type, y);
        }
    }
    for (size_t y = 0; y < decoder->height; y++)
    {
        unsigned char filter_type = decompressed_data[y * scanline_size];
        unsigned char *scanline = decompressed_data + y * scanline_size + 1;

        filter_counts[filter_type]++;

        switch (filter_type)
        {
        case 0:
            no_filter(current_output, scanline, bytes_per_pixel, decoder->width);
            break;
        case 1:
            sub_filter(current_output, scanline, bytes_per_pixel, decoder->width);
            break;
        case 2:
            up_filter(current_output, scanline, prev_scanline, bytes_per_pixel, decoder->width);
            break;
        case 3:
            average_filter(current_output, scanline, prev_scanline, bytes_per_pixel, decoder->width);
            break;
        case 4:
            paeth_filter(current_output, scanline, prev_scanline, bytes_per_pixel, decoder->width);
            break;
        default:
            fprintf(stderr, "Unsupported filter type: %u\n", filter_type);
            free(output);
            return NULL;
        }

        prev_scanline = current_output;
        current_output += decoder->width * bytes_per_pixel;
    }

    // Print filter counts
    printf("Filter Counts:\n");
    for (size_t i = 0; i < 5; i++)
    {
        printf("Filter %zu: %zu times\n", i, filter_counts[i]);
    }

    return output;
}
#pragma endregion
#pragma region Utilities
// __builtin_bswap32 is highly optimized and translates directly to
// architecture-specific assembly instructions.
// For example, on x86, it compiles to a single BSWAP instruction.
// It ensures portability by working across different platforms
// and architectures, efficiently handling byte swapping without
// the need for manual implementation.
// Using __builtin_bswap32 simplifies code and reduces complexity.
uint32_t to_big_endian(uint8_t *bytes)
{
    uint32_t value;
    memcpy(&value, bytes, 4);

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value); // Little-Endian to Big-Endian if neccessary
#endif
    return value;
}
void print_PNG_info(PNG_decoder_t *decoder)
{
    printf("PNG Information:\n");
    printf("Width: %u px\n", decoder->width);
    printf("Height: %u px\n", decoder->height);
    printf("Bit Depth: %u\n", decoder->bit_depth);

    printf("Color Type: %u\n", decoder->color_type);
    switch (decoder->color_type)
    {
    case 0:
        printf(" (Grayscale)\n");
        break;
    case 2:
        printf(" (Truecolor)\n");
        break;
    case 3:
        printf(" (Indexed-color)\n");
        break;
    case 4:
        printf(" (Grayscale with Alpha)\n");
        break;
    case 6:
        printf(" (Truecolor with Alpha)\n");
        break;
    default:
        printf(" (Unknown)\n");
        break;
    }

    printf("Compression Method: %u\n", decoder->compression_method);
    printf("Filter Method: %u\n", decoder->filter_method);

    printf("Interlace Method: %u\n", decoder->interlace_method);
    if (decoder->interlace_method == 0)
    {
        printf(" (No interlace)\n");
    }
    else if (decoder->interlace_method == 1)
    {
        printf(" (Adam7 interlace)\n");
    }
    else
    {
        printf(" (Unknown interlace method)\n");
    }
}
#pragma endregion

#pragma endregion