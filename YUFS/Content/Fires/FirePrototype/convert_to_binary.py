import pandas as pd
import struct
import glob
import os

def convert_multiple_csv_to_binary(folder_path, output_bin):
    # 1. 파일 목록 정렬 (dataset_000000.csv ~ dataset_004000.csv)
    file_list = sorted(glob.glob(os.path.join(folder_path, "dataset_*.csv")))
    total_frames = len(file_list)
    
    if total_frames == 0:
        print("No files found!")
        return

    # 격자 크기 정보 (고정된 경우)
    dim_x, dim_y, dim_z = 153, 115, 17
    
    print(f"Found {total_frames} files. Starting conversion...")

    with open(output_bin, 'wb') as f:
        # 헤더 기록 (프레임 수, X, Y, Z)
        f.write(struct.pack('iiii', total_frames, dim_x, dim_y, dim_z))
        
        for i, file_path in enumerate(file_list):
            # 2. CSV 로드 (좌표는 순서가 일정하므로 density, temperature만 읽음)
            # usecols를 사용해 필요한 컬럼만 빠르게 로드
            df = pd.read_csv(file_path)

            df = df.sort_values(
    by=['Points:0', 'Points:1', 'Points:2']
)
            
            # 3. uint16 배열로 변환
            # CSV에 이미 정수로 적혀있으므로 바로 변환
            densities = df['density'].astype('uint8').values
            temperatures = df['temperature'].astype('uint8').values
            
            # 4. 바이너리 쓰기 ('H'는 unsigned short, 2바이트)
            f.write(densities.tobytes())
            f.write(temperatures.tobytes())
            
            if i % 100 == 0:
                print(f"Progress: {i}/{total_frames} frames processed.")

    print(f"Done! Saved to {output_bin}")

# 실행 (CSV 파일들이 있는 폴더 경로 입력)
convert_multiple_csv_to_binary('./', 'smoke_data.bin')