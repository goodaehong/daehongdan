from pathlib import Path
import argparse
import math

from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas


def build_pdf(
    output: Path,
    image_dir: Path,
    marker_ids: list[int],
    title: str,
    black_marker_mm: float,
) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    pdf = canvas.Canvas(str(output), pagesize=A4, pageCompression=1)
    width, height = A4
    marker_image_mm = black_marker_mm * 4.0 / 3.0
    if black_marker_mm <= 45.0:
        positions = [
            (x * mm, y * mm)
            for y in (205, 115, 28)
            for x in (35, 105, 175)
        ]
    else:
        positions = [
            (52.5 * mm, 185 * mm),
            (157.5 * mm, 185 * mm),
            (52.5 * mm, 55 * mm),
            (157.5 * mm, 55 * mm),
        ]

    markers_per_page = len(positions)
    pages = math.ceil(len(marker_ids) / markers_per_page)
    for page_index in range(pages):
        pdf.setFont("Helvetica-Bold", 12)
        pdf.drawCentredString(width / 2, height - 12 * mm, title)
        pdf.setFont("Helvetica", 8)
        pdf.drawCentredString(
            width / 2, height - 18 * mm,
            "Print at Actual Size / 100% - Do not use Fit to Page")

        first = page_index * markers_per_page
        last = (page_index + 1) * markers_per_page
        for slot, marker_id in enumerate(marker_ids[first:last]):
            center_x, image_y = positions[slot]
            image_x = center_x - (marker_image_mm * mm / 2)
            image_path = image_dir / f"aruco_4x4_50_id_{marker_id:02d}.png"
            if not image_path.exists():
                raise FileNotFoundError(image_path)

            # Light cut guide outside the mandatory white quiet zone.
            pdf.setStrokeColorRGB(0.75, 0.75, 0.75)
            pdf.setLineWidth(0.25)
            pdf.rect(
                center_x - (marker_image_mm / 2 + 4) * mm,
                image_y - 12 * mm,
                (marker_image_mm + 8) * mm,
                (marker_image_mm + 16) * mm,
                stroke=1, fill=0)
            pdf.drawImage(
                str(image_path), image_x, image_y,
                marker_image_mm * mm, marker_image_mm * mm,
                preserveAspectRatio=True, mask="auto")
            pdf.setFillColorRGB(0, 0, 0)
            pdf.setFont("Helvetica-Bold", 11)
            pdf.drawCentredString(
                center_x, image_y - 5.5 * mm,
                f"DICT_4X4_50 - ID {marker_id}")
            pdf.setFont("Helvetica", 7.5)
            pdf.drawCentredString(
                center_x, image_y - 9.5 * mm,
                f"black square = {black_marker_mm:.0f} mm")

        pdf.setFont("Helvetica", 7)
        pdf.drawRightString(
            width - 10 * mm, 8 * mm, f"page {page_index + 1}/{pages}")
        pdf.showPage()

    pdf.save()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--images", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--ids", required=True, help="comma/range, e.g. 0-20 or 20-24")
    parser.add_argument("--title", required=True)
    parser.add_argument("--black-mm", type=float, default=40.0)
    args = parser.parse_args()

    marker_ids: list[int] = []
    for part in args.ids.split(","):
        if "-" in part:
            first, last = (int(value) for value in part.split("-", 1))
            marker_ids.extend(range(first, last + 1))
        else:
            marker_ids.append(int(part))
    build_pdf(
        Path(args.output), Path(args.images), marker_ids,
        args.title, args.black_mm)
    print(f"created {args.output} with {len(marker_ids)} markers")


if __name__ == "__main__":
    main()
