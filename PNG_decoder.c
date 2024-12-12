#include "zlib.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

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

    //Decompression
    unsigned char *decompressed_data = NULL;
    size_t decompressed_size = 0;

    if (decompress_IDAT(&decoder, &decompressed_data, &decompressed_size) != 0) {
        fprintf(stderr, "Failed to decompress IDAT data.\n");
        free(decoder.data);
        free(decoder.idat_data);
        for (size_t i = 0; i < decoder.text_count; i++) {
            free(decoder.texts[i]);
        }
        free(decoder.texts);
        return EXIT_FAILURE;
    }

    printf("\nDecompressed Data Size: %zu bytes\n", decompressed_size);

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
int decompress_IDAT(PNG_decoder_t *decoder, unsigned char **out_data, size_t *out_size) {

    //STANDARD METHOD FROM .zlib IN C FOR DECOMPRESSION

    z_stream stream;
    memset(&stream, 0, sizeof(stream));

    if (inflateInit(&stream) != Z_OK) {
        fprintf(stderr, "Failed to initialize zlib for decompression.\n");
        return -1;
    }

    // Imposta l'input (dati IDAT compressi)
    stream.next_in = decoder->idat_data;
    stream.avail_in = decoder->idat_size;

    // Alloca un buffer per i dati decompressi (dimensione approssimativa: width * height * 4 bytes per pixel)
    size_t buffer_size = decoder->width * decoder->height * 4 + decoder->height; // Include byte filtro
    *out_data = (unsigned char *)malloc(buffer_size);
    if (!(*out_data)) {
        fprintf(stderr, "Failed to allocate memory for decompressed data.\n");
        inflateEnd(&stream);
        return -1;
    }

    stream.next_out = *out_data;
    stream.avail_out = buffer_size;

    // Decomprime i dati
    int ret = inflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        fprintf(stderr, "Failed to decompress IDAT data: %d\n", ret);
        free(*out_data);
        inflateEnd(&stream);
        return -1;
    }

    // Ritorna la dimensione effettiva dei dati decompressi
    *out_size = stream.total_out;

    // Libera risorse zlib
    inflateEnd(&stream);

    return 0;
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