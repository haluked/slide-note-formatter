#ifndef PDF_ENGINE_H
#define PDF_ENGINE_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int slides_per_page;      /* 2, 3, or 4 (default: 3) */
    int draw_border;          /* 1 = draw subtle gray border around slide, 0 = no border */
    int draw_note_lines;      /* 1 = draw ruled lines in the note column, 0 = blank */
    int add_cover;            /* 1 = include front cover page, 0 = omit */
    int add_end;              /* 1 = include end-of-module page, 0 = omit */

    int note_area_enabled;    /* 1 = reserve a note/annotation column, 0 = slides use full width */
    int note_side;            /* 0 = note column on the right, 1 = on the left */
    double note_width_mm;     /* space reserved from the outer page edge (default: 84) */
    double slide_gap_mm;      /* vertical gap between slide slots (default: 5) */

    double full_page_aspect;  /* pages narrower than this are tall/portrait pages (default: 0.9) */

    const char *course_name;  /* Course / Subject title (e.g. "Computer Architecture") */
    const char *module_name;  /* Module name / subtitle (e.g. "Lecture 01 - Intro") */
    const char *author_name;  /* Student / Author name for watermark and cover */
    const char *university;   /* University / Institution name */
} PDFEngineConfig;

/* Fill a config with the application defaults. Call this before overriding fields. */
void pdf_engine_config_init(PDFEngineConfig *config);


/* Progress callback: current_slide (0-based), total_slides */
typedef void (*PDFProgressCallback)(int current_slide, int total_slides, void *user_data);

/* Process an input PDF with wide-character (Unicode) file paths.
   Returns 0 on success, non-zero error code on failure.
*/
int process_pdf_file_w(
    const wchar_t *input_path,
    const wchar_t *output_path,
    const PDFEngineConfig *config,
    PDFProgressCallback progress_cb,
    void *user_data,
    int *out_pages,
    int *out_slides,
    char *error_msg,
    size_t error_msg_size
);

/* UTF-8 convenience wrapper */
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
