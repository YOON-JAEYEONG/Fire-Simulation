import numpy as np
import os
import sys
import argparse

def getGridFromSMV(path):
    with open(path) as f:
        lines = [l.strip() for l in f.readlines()]
        lines = [l for l in lines if l!=""]
    #Get the begin
    begin = [0,0,0]
    for i,l in enumerate(lines):
        for j,kwd in enumerate(["TRNX", "TRNY", "TRNZ"]):
            if kwd in l:
                begin[j] = i+2
    #Write the X, Y and Z coords
    currentInd = 0
    X, Y, Z = [], [], []
    for i,l in enumerate(lines):
        if i>=begin[currentInd]:
            try:
                if currentInd==0:
                    X.append(float(l.split()[1]))
                if currentInd==1:
                    Y.append(float(l.split()[1]))
                if currentInd==2:
                    Z.append(float(l.split()[1]))
            except:
                if currentInd == 2:
                    break
                else:
                    currentInd+=1
    print(len(X), len(Y), len(Z))
    return X,Y,Z
def getOutputFilesFromSMV(path):
    with open(path) as f:
        files = []
        lines = [l.strip() for l in f.readlines()]
        lines = [l for l in lines if l!=""]
        for i in range(len(lines)):
            if lines[i].split()[0]=="SMOKF3D":
                files.append([lines[i+2].split()[0], lines[i+1].strip()])
            if lines[i].split()[0]=="SLCF":
                files.append([lines[i+2].strip(), lines[i+1].strip()])
        return files
def toInt(byte):
    if isinstance(byte, int):
        return byte
    return byte[0]
def readS3D(s3dfile, frames, start=0, end=None):
    if end is None:
        end = len(frames)

    VALUES = []
    with open(s3dfile, "rb") as f:
        #First offset
        X = 36
        f.seek(X,1)

        #Skipping until the start
        for frame in frames[:start]:
            f.seek(72-X, 1)
            f.seek(int(frame[2]), 1)

        #Reading from start to end
        if start!=0 or end!=len(frames):
            print ("Reading the frames " + str(start) + " to " + str(end))
        for ind, frame in enumerate(frames[start:end]):
            f.seek(72-X, 1)
            bits  = f.read(int(frame[2]))
            vals  = []
            i     = 0
            while(i<len(bits)):
                if toInt(bits[i]) == 255:
                    val = toInt(bits[i+1])
                    n   = toInt(bits[i+2])
                    for j in range(n):
                        vals.append(val)
                    i+=3
                else:
                    val = toInt(bits[i])
                    vals.append(val)
                    i+=1
            VALUES.append(vals)
    return VALUES
def writeVTKArray(arr, f):
    for i in range(len(arr) // 6 + 1):
        for j in range(6):
            if 6 * i + j < len(arr):
                f.write(str(arr[6 * i + j]) + " ")
        f.write("\n")
def exportVTK(filepath, name, x,y,z,values):
    with open(filepath, "w") as f:
        writeVTKArray(values, f)
def arguments():
    parser = argparse.ArgumentParser(description='Converts FDS outputs to vtk.')
    parser.add_argument('--input',   '-i', help='simulation root path', required=False, default="C:\\FDS_6FLOOR")
    parser.add_argument('--run',     '-r', help='really run', action="store_true", default=True)
    parser.add_argument('--output',  '-o', help='output folder (default to input folder)', default="C:\\FDS_6FLOOR\\output")
    parser.add_argument('--start',   '-s', help='frame to start extraction (default to first)', type=int, default=0)
    parser.add_argument('--end',     '-e', help='frame to end extraction (default to last available)', type=int, default=0)
    args = parser.parse_args()

    if not os.path.isdir(args.input):
        print (args.input + " not a directory")
        sys.exit()
    if len([f for f in os.listdir(args.input) if f[-4:]==".smv"])==0:
        print ("No .smv file in " + args.input)
        sys.exit()
    args.input = os.path.abspath(args.input)

    if args.output is not None:
        if not os.path.isdir(args.output):
            print (args.output + " does not exist, please create it")
            sys.exit()
    else:
        args.output = args.input
    args.output = os.path.abspath(args.output)

    return args

if __name__=="__main__":
    # 0 - Argument parsing
    args = arguments()
    case = [f[:-4] for f in os.listdir(args.input) if f[-4:]==".smv"][0]

    # 1 - Get the grid info
    _x,_y,_z = getGridFromSMV(os.path.join(args.input, case + ".smv"))

    # 2 - Get the output data files (.s3d and .sf)
    files = getOutputFilesFromSMV(os.path.join(args.input, case + ".smv"))

    # 3 - Loop on the data files
    if args.run:
        for f in files:
            #Read the .s3d files
            if f[1][-4:] == ".s3d":
                folder_path = os.path.join(args.output, f[0])
                if not os.path.exists(folder_path):
                    os.makedirs(folder_path)

                with open( os.path.join(args.input, f[1] + ".sz") ) as fsz:
                    frames = np.array([[float(x) for x in l.split()] for l in fsz.readlines()[1:]])

                args.end = len(frames)

                values = readS3D( os.path.join(args.input, f[1]) , frames , args.start, args.end)
                print (len(values))

                for i,v in enumerate(values):
                    vtkFile = os.path.join(folder_path, str(args.start + i) + ".txt")
                    print ("Writing " + vtkFile)
                    exportVTK(vtkFile , f[0], _x, _y, _z, v)


    else:
        print (args)
        print (case)
        print (len(_x), len(_y), len(_z))
        for f in files:
            print (f)
        print ("To run the script, add the --run option")
