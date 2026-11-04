import os
import fitz
from PIL import Image

def process_pdf(
    input_path,
    output_path,
    config,
    course_name="",
    progress_callback=None,
    logo_bytes=None
):
    """
    Formats a slide presentation PDF into an A4 note-taking study handout.
    """
    src = fitz.open(input_path)
    total_slides = len(src)

    A4_W, A4_H = 595.28, 841.89  # Standard A4 in points
    slides_per_page = int(config.get("slides_per_page", 3))
    slot_h = A4_H / float(slides_per_page)
    slide_w = 270.0
    left_margin = 32.0

    crop_mm = float(config.get("crop_mm", 1.0))
    crop_pt = crop_mm * 72.0 / 25.4

    dpi = int(config.get("dpi", 144))
    jpg_quality = int(config.get("jpg_quality", 75))
    is_gray = bool(config.get("grayscale", True))
    draw_border = bool(config.get("draw_border", False))

    author = str(config.get("user_name", "")).strip()
    university = str(config.get("university_name", "")).strip()
    subtitle = str(config.get("subtitle", "Lecture Notes & Study Handouts")).strip()
    add_cover = bool(config.get("add_cover", True))
    add_end = bool(config.get("add_end", True))

    arial_path = r"C:\Windows\Fonts\arial.ttf"
    arial_bd = r"C:\Windows\Fonts\arialbd.ttf"
    has_arial = os.path.isfile(arial_path)

    fname = os.path.splitext(os.path.basename(input_path))[0]
    module_text = fname

    final_doc = fitz.open()

    # 1. FRONT COVER PAGE
    if add_cover:
        cov = final_doc.new_page(width=A4_W, height=A4_H)
        if logo_bytes:
            try:
                cov.insert_image(fitz.Rect(180, 110, 415, 207), stream=logo_bytes)
            except Exception as e:
                print("Logo render warning:", e)

        cov.draw_line((80, 250), (515, 250), color=(0.2, 0.2, 0.2), width=1.0)

        display_course = course_name if course_name else module_text

        if has_arial:
            if university:
                cov.insert_textbox(fitz.Rect(80, 290, 515, 340), university, fontsize=14, fontfile=arial_bd, fontname="arialbd", align=fitz.TEXT_ALIGN_CENTER)
            cov.insert_textbox(fitz.Rect(80, 350, 515, 420), display_course, fontsize=18, fontfile=arial_bd, fontname="arialbd", align=fitz.TEXT_ALIGN_CENTER)
            if course_name and course_name != module_text:
                cov.insert_textbox(fitz.Rect(80, 430, 515, 480), module_text, fontsize=14, fontfile=arial_path, fontname="arial", align=fitz.TEXT_ALIGN_CENTER)

            if author:
                cov.draw_line((180, 510), (415, 510), color=(0.6, 0.6, 0.6), width=0.5)
                cov.insert_textbox(fitz.Rect(80, 550, 515, 590), author, fontsize=13, fontfile=arial_bd, fontname="arialbd", align=fitz.TEXT_ALIGN_CENTER)
                if subtitle:
                    cov.insert_textbox(fitz.Rect(80, 590, 515, 620), subtitle, fontsize=10, fontfile=arial_path, fontname="arial", align=fitz.TEXT_ALIGN_CENTER, fill_opacity=0.7)
            elif subtitle:
                cov.draw_line((180, 510), (415, 510), color=(0.6, 0.6, 0.6), width=0.5)
                cov.insert_textbox(fitz.Rect(80, 560, 515, 600), subtitle, fontsize=11, fontfile=arial_path, fontname="arial", align=fitz.TEXT_ALIGN_CENTER, fill_opacity=0.7)
        else:
            if university:
                cov.insert_textbox(fitz.Rect(80, 290, 515, 340), university, fontsize=14, align=fitz.TEXT_ALIGN_CENTER)
            cov.insert_textbox(fitz.Rect(80, 350, 515, 420), display_course, fontsize=18, align=fitz.TEXT_ALIGN_CENTER)
            if course_name and course_name != module_text:
                cov.insert_textbox(fitz.Rect(80, 430, 515, 480), module_text, fontsize=14, align=fitz.TEXT_ALIGN_CENTER)

            if author:
                cov.draw_line((180, 510), (415, 510), color=(0.6, 0.6, 0.6), width=0.5)
                cov.insert_textbox(fitz.Rect(80, 550, 515, 590), author, fontsize=13, align=fitz.TEXT_ALIGN_CENTER)
                if subtitle:
                    cov.insert_textbox(fitz.Rect(80, 590, 515, 620), subtitle, fontsize=10, align=fitz.TEXT_ALIGN_CENTER, fill_opacity=0.7)
            elif subtitle:
                cov.draw_line((180, 510), (415, 510), color=(0.6, 0.6, 0.6), width=0.5)
                cov.insert_textbox(fitz.Rect(80, 560, 515, 600), subtitle, fontsize=11, align=fitz.TEXT_ALIGN_CENTER, fill_opacity=0.7)

    # 2. SLIDES WITH NOTES
    for i in range(0, total_slides, slides_per_page):
        if progress_callback:
            progress_callback(i, total_slides)

        page = final_doc.new_page(width=A4_W, height=A4_H)

        # Header Watermark
        if author:
            tb = fitz.Rect(A4_W - 250, 18, A4_W - 32, 32)
            opacity = float(config.get("watermark_opacity", 0.5))
            fsize = int(config.get("watermark_fontsize", 7))
            if has_arial:
                page.insert_textbox(tb, author, fontsize=fsize, fontfile=arial_path, fontname="arial", align=fitz.TEXT_ALIGN_RIGHT, fill_opacity=opacity)
            else:
                page.insert_textbox(tb, author, fontsize=fsize, align=fitz.TEXT_ALIGN_RIGHT, fill_opacity=opacity)

        for s_idx in range(slides_per_page):
            s_num = i + s_idx
            if s_num >= total_slides:
                break

            p = src[s_num]
            clip = fitz.Rect(
                p.rect.x0 + crop_pt,
                p.rect.y0 + crop_pt,
                p.rect.x1 - crop_pt,
                p.rect.y1 - crop_pt
            )
            aspect = clip.height / clip.width
            slide_h = slide_w * aspect
            y_top = (s_idx * slot_h) + (slot_h - slide_h) / 2.0
            r = fitz.Rect(left_margin, y_top, left_margin + slide_w, y_top + slide_h)

            cs = fitz.csGRAY if is_gray else fitz.csRGB
            pix = p.get_pixmap(dpi=dpi, colorspace=cs, clip=clip)
            jpg_bytes = pix.tobytes("jpeg", jpg_quality=jpg_quality)
            page.insert_image(r, stream=jpg_bytes)

            if draw_border:
                page.draw_rect(r, color=(0.75, 0.75, 0.75), width=0.5)

    # 3. END PAGE
    if add_end:
        end_page = final_doc.new_page(width=A4_W, height=A4_H)
        display_course = course_name if course_name else module_text
        if has_arial:
            end_page.insert_textbox(fitz.Rect(80, 330, 515, 390), display_course, fontsize=16, fontfile=arial_bd, fontname="arialbd", align=fitz.TEXT_ALIGN_CENTER)
            if course_name and course_name != module_text:
                end_page.insert_textbox(fitz.Rect(80, 390, 515, 430), module_text, fontsize=12, fontfile=arial_path, fontname="arial", align=fitz.TEXT_ALIGN_CENTER)
            end_page.draw_line((180, 450), (415, 450), color=(0.6, 0.6, 0.6), width=0.5)
            end_page.insert_textbox(fitz.Rect(80, 470, 515, 510), "- END OF MODULE -", fontsize=11, fontfile=arial_bd, fontname="arialbd", align=fitz.TEXT_ALIGN_CENTER)
            if author:
                end_page.insert_textbox(fitz.Rect(80, 530, 515, 570), author, fontsize=11, fontfile=arial_path, fontname="arial", align=fitz.TEXT_ALIGN_CENTER, fill_opacity=0.7)
        else:
            end_page.insert_textbox(fitz.Rect(80, 330, 515, 390), display_course, fontsize=16, align=fitz.TEXT_ALIGN_CENTER)
            if course_name and course_name != module_text:
                end_page.insert_textbox(fitz.Rect(80, 390, 515, 430), module_text, fontsize=12, align=fitz.TEXT_ALIGN_CENTER)
            end_page.draw_line((180, 450), (415, 450), color=(0.6, 0.6, 0.6), width=0.5)
            end_page.insert_textbox(fitz.Rect(80, 470, 515, 510), "- END OF MODULE -", fontsize=11, align=fitz.TEXT_ALIGN_CENTER)
            if author:
                end_page.insert_textbox(fitz.Rect(80, 530, 515, 570), author, fontsize=11, align=fitz.TEXT_ALIGN_CENTER, fill_opacity=0.7)

    final_doc.save(output_path, deflate=True)
    out_count = len(final_doc)
    final_doc.close()
    src.close()

    return {"pages": out_count, "slides": total_slides}
