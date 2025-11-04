rm build/* -rf
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ../
cmake --build . --parallel 4
cp MNNMemoryAllocator ../ 
