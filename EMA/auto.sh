rm build -rf
mkdir build
cd build
cmake ../
cmake --build . --parallel 4
cp EMA ../ 
