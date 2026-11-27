
""" 
    Kullanim (ASM503 dizininden):
   .venv\Scripts\python scripts\pyfing_example.py [goruntu_yolu]
"""
import sys
import cv2 as cv
import pyfing as pf

path = sys.argv[1] if len(sys.argv) > 1 else '../FVC/FVC2002/Dbs/Db1_b/101_1.tif'

fingerprint = cv.imread(path, cv.IMREAD_GRAYSCALE)
if fingerprint is None:
    sys.exit(f'Goruntu okunamadi: {path}')

segmentation_mask = pf.fingerprint_segmentation(fingerprint)
orientations = pf.orientation_field_estimation(fingerprint, segmentation_mask)
frequencies = pf.frequency_estimation(fingerprint, orientations, segmentation_mask)

enhanced_image = pf.fingerprint_enhancement(fingerprint, orientations, frequencies, segmentation_mask, method="GBFEN")

minutiae = pf.minutiae_extraction(enhanced_image, method="SBMEX", segmentation_mask=segmentation_mask)

minutiae = pf.minutiae_extraction(fingerprint)

print(f'{path} ({fingerprint.shape[1]}x{fingerprint.shape[0]})')
print(f'Minutiae sayisi: {len(minutiae)}')
for m in minutiae:
    print(m)
