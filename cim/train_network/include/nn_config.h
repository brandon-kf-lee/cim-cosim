/* 
 * Defines the sizes of various neural networks in use
 * Author: Brandon Lee, brandon.kf.lee@gmail.com
 */ 

#ifndef NN_CONFIG_H
#define NN_CONFIG_H

// ==========================================
// NETWORK TOGGLE
// 0 = MNIST (784 x 10)
// 1 = Large Synthetic NN (1024 x 1024)
// ==========================================
#if USE_SYNTHETIC_NETWORK
    #define NN_IN_SIZE 1024
    #define NN_OUT_SIZE 1024
#else
    #define NN_IN_WIDTH 28
    #define NN_IN_HEIGHT 28
    #define NN_IN_SIZE (NN_IN_WIDTH * NN_IN_HEIGHT)  // 784
    #define NN_OUT_SIZE 10
#endif

#endif // NN_CONFIG_H

