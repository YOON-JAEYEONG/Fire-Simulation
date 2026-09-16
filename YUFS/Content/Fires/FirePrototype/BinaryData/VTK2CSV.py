import os
import re
from fractions import Fraction
from multiprocessing import Pool, cpu_count


def count_files_in_directory(directory_path):
    return sum([1 for item in os.listdir(directory_path) if os.path.isfile(os.path.join(directory_path, item))])


def getNums(file_path: str):
    with open(file_path, 'r') as file:
        file_contents = file.read()
        nums = re.findall(r"\d+", file_contents)

    return nums


def process_step(args):
    current_step, point_count, x_gap, y_gap, z_gap, x_count, y_count, z_count = args

    cur_soot_path = os.path.join("SOOT", f"{current_step}.txt")
    cur_temperature_path = os.path.join("HRRPUV", f"{current_step}.txt")

    cur_soot = getNums(cur_soot_path)
    cur_temperature = getNums(cur_temperature_path)

    # 이 프로세스에서의 cur_soot와 cur_temperature의 최대, 최소 값을 찾습니다.
    max_soot = max(map(int, cur_soot))
    min_soot = min(map(int, cur_soot))
    max_temperature = max(map(int, cur_temperature))
    min_temperature = min(map(int, cur_temperature))

    with open(f"csv/dataset_{current_step:06}.csv", "w") as file:
        # first line
        file.write("Points:0,Points:1,Points:2,density,temperature\n")

        x, y, z = Fraction(0, 1), Fraction(0, 1), Fraction(0, 1)
        for point in range(0, point_count):
            file.write(f"{float(x)},{float(y)},{float(z)},{cur_soot[point]},{cur_temperature[point]}\n")
            x += x_gap
            if (x > x_gap * (x_count - 1)):
                x = 0
                y += y_gap

            if (y > y_gap * (y_count - 1)):
                y = 0
                z += z_gap

        print(f"csv/dataset_{current_step:06}.csv finish")

    # 최대, 최소 값을 반환합니다.
    return max_soot, min_soot, max_temperature, min_temperature


if __name__ == "__main__":  # multiprocessing은 이 구문 아래에서 실행해야 합니다.
    max_time_step = count_files_in_directory("SOOT")

    x_count = 181
    y_count = 79
    z_count = 18
    point_count = x_count * y_count * z_count

    x_gap = Fraction(2, 5)  # 0.4m
    y_gap = Fraction(2, 5)  # 0.4m
    z_gap = Fraction(2, 5)  # 0.4m

    if not os.path.exists("csv"):
        os.mkdir("csv")

    # 병렬 처리 시작
    pool = Pool(processes=cpu_count())  # 사용 가능한 모든 CPU 코어를 사용
    args = [(step, point_count, x_gap, y_gap, z_gap, x_count, y_count, z_count) for step in range(max_time_step)]

    # 각 프로세스에서 반환된 최대, 최소 값들을 모은다.
    results = pool.map(process_step, args)

    # 모든 프로세스에서 반환된 최대, 최소 값들 중에서 전체 최대, 최소 값을 찾는다.
    overall_max_soot = max(result[0] for result in results)
    overall_min_soot = min(result[1] for result in results)
    overall_max_temperature = max(result[2] for result in results)
    overall_min_temperature = min(result[3] for result in results)

    print(f"Overall Max Soot: {overall_max_soot}")
    print(f"Overall Min Soot: {overall_min_soot}")
    print(f"Overall Max Temperature: {overall_max_temperature}")
    print(f"Overall Min Temperature: {overall_min_temperature}")

    pool.close()
    pool.join()