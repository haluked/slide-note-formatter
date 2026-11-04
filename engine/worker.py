import sys
import os
import json
import traceback

# Add parent and local dir to sys.path
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from pdf_engine import process_pdf
from assets import get_logo_bytes

def emit(msg_type, **kwargs):
    payload = {"type": msg_type, **kwargs}
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()

def main():
    if len(sys.argv) < 2:
        emit("error", message="Missing job configuration argument")
        sys.exit(1)

    job_arg = sys.argv[1]
    if os.path.isfile(job_arg):
        with open(job_arg, "r", encoding="utf-8") as f:
            data = json.load(f)
    else:
        data = json.loads(job_arg)

    files = data.get("files", [])
    course_name = data.get("course_name", "")
    config = data.get("config", {})
    custom_dest = config.get("custom_dest_folder", "")
    dest_mode = config.get("dest_mode", "subfolder")

    total_files = len(files)
    if total_files == 0:
        emit("done", success_count=0, total_files=0, output_dir="", errors=[])
        return

    custom_logo_path = config.get("custom_logo_path", "")
    logo_bytes = get_logo_bytes(custom_logo_path)

    success_count = 0
    errors = []
    last_output_dir = ""

    for file_idx, pdf_path in enumerate(files):
        try:
            base_dir = os.path.dirname(pdf_path)
            file_name = os.path.basename(pdf_path)
            name_without_ext = os.path.splitext(file_name)[0]

            if dest_mode == "custom" and custom_dest and os.path.isdir(custom_dest):
                target_dir = custom_dest
            else:
                target_dir = os.path.join(base_dir, "_Not_Formatli")

            os.makedirs(target_dir, exist_ok=True)
            last_output_dir = target_dir

            s_count = config.get("slides_per_page", 3)
            out_filename = f"{name_without_ext} ({s_count} Slayt Not Alanli).pdf"
            out_path = os.path.join(target_dir, out_filename)

            def p_callback(cur_slide, total_s):
                sub_prog = (file_idx + (cur_slide / max(total_s, 1))) / total_files
                emit(
                    "progress",
                    file_idx=file_idx,
                    total_files=total_files,
                    file_name=file_name,
                    slide=cur_slide,
                    total_slides=total_s,
                    progress=round(sub_prog, 4)
                )

            process_pdf(
                input_path=pdf_path,
                output_path=out_path,
                config=config,
                course_name=course_name,
                progress_callback=p_callback,
                logo_bytes=logo_bytes
            )
            success_count += 1
            emit(
                "file_done",
                file_idx=file_idx,
                file_name=file_name,
                out_path=out_path
            )

        except Exception as e:
            err_str = str(e)
            traceback.print_exc(file=sys.stderr)
            errors.append(f"{os.path.basename(pdf_path)}: {err_str}")
            emit("file_error", file_name=os.path.basename(pdf_path), error=err_str)

    emit(
        "done",
        success_count=success_count,
        total_files=total_files,
        output_dir=last_output_dir,
        errors=errors
    )

if __name__ == "__main__":
    main()
