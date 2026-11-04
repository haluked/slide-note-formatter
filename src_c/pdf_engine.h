#ifndef PDF_ENGINE_H
#define PDF_ENGINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int slides_per_page;      /* 2, 3, or 4 (default: 3) */
    int draw_border;          /* 1 = draw subtle gray border around slide, 0 = no border */
    int draw_note_lines;      /* 1 = draw ruled lines on right side for note-taking, 0 = blank */
    int add_cover;            /* 1 = include front cover page, 0 = omit */
    int add_end;              /* 1 = include end-of-module page, 0 = omit */
    const char *course_name;  /* Course / Subject title (e.g. "Computer Architecture") */
    const char *module_name;  /* Module name / subtitle (e.g. "Lecture 01 - Intro") */
    const char *author_name;  /* Student / Author name for watermark and cover */
    const char *university;   /* University / Institution name */
} PDFEngineConfig;

/* Progress callback: current_slide (0-based), total_slides */
typedef void (*PDFProgressCallback)(int current_slide, int total_slides, void *user_data);

/* Process an input PDF and generate the formatted handout PDF.
   Returns 0 on success, non-zero error code on failure.
   If out_pages is non-NULL, writes the number of output pages generated.
   If out_slides is non-NULL, writes the number of input slides processed.
   If error_msg is non-NULL and error_msg_size > 0, writes an error description on failure.
*/
int process_pdf_file(
    const char *input_path,
    const char *output_path,
    const PDFEngineConfig *config,
    PDFProgressCallback progress_cb,
    void *user_data,
    int *out_pages,
    int *out_slides,
    char *error_msg,
    size_t error_msg_size
);

#ifdef __cplusplus
}
#endif

#endif /* PDF_ENGINE_H */
