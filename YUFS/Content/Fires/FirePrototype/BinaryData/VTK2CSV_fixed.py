import os
import re
from fractions import Fraction
from multiprocessing import Pool, cpu_count

# =========================
# 설정값
# =========================

# FDS 결과 txt 폴더 이름
# 현재 스크립트는 SOOT 값을 density로, HRRPUV 값을 temperature 컬럼으로 저장합니다.
# 실제 온도 폴더를 쓰려면 "HRRPUV"를 "TEMPERATURE"로 바꾸세요.
SOOT_DIR = "SOOT"
TEMPERATURE_DIR = "HRRPUV"

# 출력 폴더
CSV_DIR = "csv"

# 격자 크기
X_COUNT = 181
Y_COUNT = 79
Z_COUNT = 18

# 격자 간격
X_GAP = Fraction(2, 5)  # 0.4m
Y_GAP = Fraction(2, 5)  # 0.4m
Z_GAP = Fraction(2, 5)  # 0.4m

# 데이터가 부족한 지점은 0으로 채울지 여부
FILL_MISSING_WITH_ZERO = True

# 병렬 처리 여부
USE_MULTIPROCESSING = True


# 정수/소수/음수/지수 표기까지 읽기
NUMBER_PATTERN = re.compile(
    r"[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?"
)


def get_numbers(file_path: str):
    """txt 파일에서 숫자들을 float 문자열 리스트로 읽습니다.

    파일이 없으면 빈 리스트를 반환합니다.
    """
    if not os.path.exists(file_path):
        return []

    with open(file_path, "r", encoding="utf-8", errors="ignore") as file:
        file_contents = file.read()

    return NUMBER_PATTERN.findall(file_contents)


def get_step_numbers(directory_path: str):
    """폴더 안의 1.txt, 000001.txt 같은 파일명에서 step 번호를 추출합니다."""
    if not os.path.isdir(directory_path):
        raise FileNotFoundError(f"Directory not found: {directory_path}")

    steps = set()

    for filename in os.listdir(directory_path):
        full_path = os.path.join(directory_path, filename)

        if not os.path.isfile(full_path):
            continue

        name, ext = os.path.splitext(filename)

        if ext.lower() != ".txt":
            continue

        if name.isdigit():
            steps.add(int(name))

    return steps


def find_step_file(directory_path: str, step: int):
    """step 번호에 맞는 txt 파일 경로를 찾습니다.

    1.txt, 000001.txt 둘 다 지원합니다.
    """
    candidates = [
        os.path.join(directory_path, f"{step}.txt"),
        os.path.join(directory_path, f"{step:06d}.txt"),
        os.path.join(directory_path, f"{step:04d}.txt"),
    ]

    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate

    # 위 후보에 없으면 폴더를 뒤져서 숫자 파일명 비교
    if os.path.isdir(directory_path):
        for filename in os.listdir(directory_path):
            name, ext = os.path.splitext(filename)

            if ext.lower() == ".txt" and name.isdigit() and int(name) == step:
                return os.path.join(directory_path, filename)

    return None


def safe_max_int(values):
    if not values:
        return 0
    return max(int(float(v)) for v in values)


def safe_min_int(values):
    if not values:
        return 0
    return min(int(float(v)) for v in values)


def process_step(args):
    current_step, point_count, x_gap, y_gap, z_gap, x_count, y_count, z_count = args

    cur_soot_path = find_step_file(SOOT_DIR, current_step)
    cur_temperature_path = find_step_file(TEMPERATURE_DIR, current_step)

    cur_soot = get_numbers(cur_soot_path) if cur_soot_path else []
    cur_temperature = get_numbers(cur_temperature_path) if cur_temperature_path else []

    warning = None

    if len(cur_soot) != point_count or len(cur_temperature) != point_count:
        warning = (
            f"[WARNING] step={current_step}, "
            f"points={point_count}, "
            f"soot={len(cur_soot)}, "
            f"temperature={len(cur_temperature)}, "
            f"soot_file={cur_soot_path}, "
            f"temperature_file={cur_temperature_path}"
        )
        print(warning)

    max_soot = safe_max_int(cur_soot)
    min_soot = safe_min_int(cur_soot)
    max_temperature = safe_max_int(cur_temperature)
    min_temperature = safe_min_int(cur_temperature)

    os.makedirs(CSV_DIR, exist_ok=True)

    csv_path = os.path.join(CSV_DIR, f"dataset_{current_step:06d}.csv")

    with open(csv_path, "w", encoding="utf-8", newline="") as file:
        file.write("Points:0,Points:1,Points:2,density,temperature\n")

        x, y, z = Fraction(0, 1), Fraction(0, 1), Fraction(0, 1)

        for point in range(point_count):
            if point < len(cur_soot):
                soot_value = cur_soot[point]
            elif FILL_MISSING_WITH_ZERO:
                soot_value = 0
            else:
                break

            if point < len(cur_temperature):
                temperature_value = cur_temperature[point]
            elif FILL_MISSING_WITH_ZERO:
                temperature_value = 0
            else:
                break

            file.write(
                f"{float(x)},{float(y)},{float(z)},"
                f"{soot_value},{temperature_value}\n"
            )

            x += x_gap

            if x > x_gap * (x_count - 1):
                x = 0
                y += y_gap

            if y > y_gap * (y_count - 1):
                y = 0
                z += z_gap

    print(f"{csv_path} finish")

    return max_soot, min_soot, max_temperature, min_temperature, warning


if __name__ == "__main__":
    point_count = X_COUNT * Y_COUNT * Z_COUNT

    soot_steps = get_step_numbers(SOOT_DIR)
    temperature_steps = get_step_numbers(TEMPERATURE_DIR)

    # 두 폴더에 있는 step을 모두 처리합니다.
    # 한쪽 데이터가 없는 step은 0으로 채웁니다.
    steps = sorted(soot_steps | temperature_steps)

    if not steps:
        raise RuntimeError(
            f"No txt files found. Check folders: {SOOT_DIR}, {TEMPERATURE_DIR}"
        )

    print(f"Found {len(steps)} steps.")
    print(f"First step: {steps[0]}, Last step: {steps[-1]}")
    print(f"Point count per frame: {point_count}")
    print(f"SOOT steps: {len(soot_steps)}, TEMPERATURE/HRRPUV steps: {len(temperature_steps)}")

    args = [
        (step, point_count, X_GAP, Y_GAP, Z_GAP, X_COUNT, Y_COUNT, Z_COUNT)
        for step in steps
    ]

    if USE_MULTIPROCESSING:
        with Pool(processes=cpu_count()) as pool:
            results = pool.map(process_step, args)
    else:
        results = []
        for arg in args:
            results.append(process_step(arg))

    overall_max_soot = max(result[0] for result in results)
    overall_min_soot = min(result[1] for result in results)
    overall_max_temperature = max(result[2] for result in results)
    overall_min_temperature = min(result[3] for result in results)

    warnings = [result[4] for result in results if result[4]]

    if warnings:
        with open("conversion_warnings.txt", "w", encoding="utf-8") as warning_file:
            warning_file.write("\n".join(warnings))

        print(f"Warnings saved to conversion_warnings.txt ({len(warnings)} warnings)")

    print(f"Overall Max Soot: {overall_max_soot}")
    print(f"Overall Min Soot: {overall_min_soot}")
    print(f"Overall Max Temperature: {overall_max_temperature}")
    print(f"Overall Min Temperature: {overall_min_temperature}")
