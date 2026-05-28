import PyPDF2
import glob

pdfs = glob.glob(r"C:\Users\dbswo\Documents\GitHub\Fire-Simulation\YUFS\Source\*.pdf")
with open(r"C:\Users\dbswo\Documents\GitHub\Fire-Simulation\YUFS\Source\pdf_content.txt", "w", encoding="utf-8") as f:
    for p in pdfs:
        f.write(f"--- START OF PDF: {p} ---\n")
        try:
            reader = PyPDF2.PdfReader(p)
            for page in reader.pages:
                text = page.extract_text()
                if text:
                    f.write(text + "\n")
        except Exception as e:
            f.write(f"Error: {e}\n")
        f.write(f"--- END OF PDF: {p} ---\n\n")
