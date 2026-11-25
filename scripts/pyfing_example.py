# pyfing "Quick Example" (https://github.com/raffaele-cappelli/pyfing)
# fingerprint.png yerine FVC2002 DB1 101_1.tif kullanilmistir.
# Kullanim (ASM503 dizininden):
#   .venv\Scripts\python scripts\pyfing_example.py [goruntu_yolu]
import sys

import cv2 as cv
import pyfing as pf

path = sys.argv[1] if len(sys.argv) > 1 else '../FVC/FVC2002/Dbs/Db1_b/101_1.tif'

# Load the fingerprint image in grayscale
fingerprint = cv.imread(path, cv.IMREAD_GRAYSCALE)
if fingerprint is None:
    sys.exit(f'Goruntu okunamadi: {path}')

# Segmentation and feature extraction
segmentation_mask = pf.fingerprint_segmentation(fingerprint)
orientations = pf.orientation_field_estimation(fingerprint, segmentation_mask)
frequencies = pf.frequency_estimation(fingerprint, orientations, segmentation_mask)

# Image enhancement
enhanced_image = pf.fingerprint_enhancement(fingerprint, orientations, frequencies, segmentation_mask, method="GBFEN")

# Minutiae detection
minutiae = pf.minutiae_extraction(enhanced_image, method="SBMEX", segmentation_mask=segmentation_mask)

# End-to-end minutiae extraction (Note: this method does not require previous steps)
minutiae = pf.minutiae_extraction(fingerprint)

print(f'{path} ({fingerprint.shape[1]}x{fingerprint.shape[0]})')
print(f'Minutiae sayisi: {len(minutiae)}')
for m in minutiae:
    print(m)
