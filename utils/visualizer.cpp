#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <utility>
#include <cstdio>
#include <cmath>
#include <sys/stat.h>
// For External Library
#include <torch/torch.h>
#include <opencv2/opencv.hpp>
// For Original Header
#include "visualizer.hpp"


// ----------------------------------------------------------
// namespace{visualizer} -> function{save_image}
// ----------------------------------------------------------
void visualizer::save_image(const torch::Tensor image, const std::string path, const std::pair<float, float> range, const size_t cols, const size_t padding, const size_t bits){

    // (0) Initialization and Declaration
    size_t i, j, k, l;
    size_t i_dev, j_dev;
    size_t width, height, channels, mini_batch_size;
    size_t width_out, height_out;
    size_t ncol, nrow;
    int flag_in, flag_out;
    cv::Mat float_mat, normal_mat, bit_mat, RGB, BGR;
    cv::Mat sample, output;
    std::vector<cv::Mat> samples;
    torch::Tensor tensor_sq, tensor_per;

    // (1) Get Tensor Size
    mini_batch_size = image.size(0);
    channels = image.size(1);
    height = image.size(2);
    width = image.size(3);

    // (2) Judge number of channels and bits
    flag_in = CV_32FC1;
    if (channels == 1){
        if (bits == 8){
            flag_out = CV_8UC1;
        }
        else if (bits == 16){
            flag_out = CV_16UC1;
        }
        else{
            std::cerr << "Error : Bits of the image to be saved is inappropriate." << std::endl;
            std::exit(1);
        }
    }
    else if (channels == 3){
        if (bits == 8){
            flag_out = CV_8UC3;
        }
        else if (bits == 16){
            flag_out = CV_16UC3;
        }
        else{
            std::cerr << "Error : Bits of the image to be saved is inappropriate." << std::endl;
            std::exit(1);
        }
    }
    else{
        std::cerr << "Error : Channels of the image to be saved is inappropriate." << std::endl;
        std::exit(1);
    }

    // (3) Add images to the array
    auto mini_batch = image.to(torch::kCPU).chunk(mini_batch_size, /*dim=*/0);  // {N,C,H,W} ===> {1,C,H,W} + {1,C,H,W} + ...
    for (auto &tensor : mini_batch){
        tensor_sq = torch::squeeze(tensor, /*dim=*/0);  // {1,C,H,W} ===> {C,H,W}
        tensor_per = tensor_sq.permute({1, 2, 0});  // {C,H,W} ===> {H,W,C}
        if (channels == 3){
            auto tensor_vec = tensor_per.chunk(mini_batch_size, /*dim=*/2);  // {H,W,3} ===> {H,W,1} + {H,W,1} + {H,W,1}
            std::vector<cv::Mat> mv;
            for (auto &tensor_channel : tensor_vec){
                mv.push_back(cv::Mat(cv::Size(width, height), flag_in, tensor_channel.data_ptr<float>()));  // torch::Tensor ===> cv::Mat
            }
            cv::merge(mv, float_mat);  // {H,W,1} + {H,W,1} + {H,W,1} ===> {H,W,3}
        }
        else{
            float_mat = cv::Mat(cv::Size(width, height), flag_in, tensor_per.data_ptr<float>());  // torch::Tensor ===> cv::Mat
        }
        normal_mat = (float_mat - range.first) / (float)(range.second - range.first);  // [range.first, range.second] ===> [0,1]
        bit_mat = normal_mat * (std::pow(2.0, bits) - 1.0);  // [0,1] ===> [0,255] or [0,65535]
        bit_mat.convertTo(sample, flag_out);  // {32F} ===> {8U} or {16U}
        if (channels == 3){
            RGB = sample;
            cv::cvtColor(RGB, BGR, cv::COLOR_RGB2BGR);  // {R,G,B} ===> {B,G,R}
            sample = BGR;
        }
        samples.push_back(sample.clone());
    }

    // (4) Output Image Information
    ncol = (mini_batch_size < cols) ? mini_batch_size : cols;
    width_out = width * ncol + padding * (ncol + 1);
    nrow = 1 + (mini_batch_size - 1) / ncol;
    height_out =  height * nrow + padding * (nrow + 1);

    // (5) Value Substitution for Output Image
    output = cv::Mat(cv::Size(width_out, height_out), flag_out, cv::Scalar::all(0));
    for (l = 0; l < mini_batch_size; l++){
        sample = samples.at(l);
        i_dev = (l % ncol) * width + padding * (l % ncol + 1);
        j_dev = (l / ncol) * height + padding * (l / ncol + 1);
        for (j = 0; j < height; j++){
            for (i = 0; i < width; i++){
                for (k = 0; k < sample.elemSize(); k++){
                    output.data[(j + j_dev) * output.step + (i + i_dev) * output.elemSize() + k] = sample.data[j * sample.step + i * sample.elemSize() + k];
                }
            }
        }
    }

    // (6) Image Output
    cv::imwrite(path, output);

    // End Processing
    return;

}


// ----------------------------------------------------------
// namespace{visualizer} -> class{graph} -> constructor
// ----------------------------------------------------------
visualizer::graph::graph(const std::string dir_, const std::string gname_, const std::vector<std::string> label_){
    this->flag = false;
    this->dir = dir_;
    this->data_dir = this->dir + "/data";
    this->gname = gname_;
    this->graph_fname= this->dir + '/' + this->gname + ".png";
    this->data_fname= this->data_dir + '/' + this->gname + ".dat";
    this->label = label_;
    mkdir(this->dir.c_str(), S_IRWXU|S_IRWXG|S_IRWXO);
    mkdir(this->data_dir.c_str(), S_IRWXU|S_IRWXG|S_IRWXO);
}


// ----------------------------------------------------------
// namespace{visualizer} -> class{graph} -> function{plot}
// ----------------------------------------------------------
void visualizer::graph::plot(const float base, const std::vector<float> value){

    // (1) Value Output
    std::ofstream ofs(this->data_fname, std::ios::app);
    ofs << base << std::flush;
    for (auto &v : value){
        ofs << ' ' << v << std::flush;
    }
    ofs << std::endl;
    ofs.close();

    // (2) Graph Output
    if (this->flag){
        FILE* gp;
        gp = popen("gnuplot -persist", "w");
        fprintf(gp, "set terminal png\n");
        fprintf(gp, "set output '%s'\n", this->graph_fname.c_str());
        fprintf(gp, "plot ");
        for (size_t i = 0; i < this->label.size() - 1; i++){
            fprintf(gp, "'%s' using 1:%zu ti '%s' with lines,", this->data_fname.c_str(), i + 2, this->label.at(i).c_str());
        }
        fprintf(gp, "'%s' using 1:%zu ti '%s' with lines\n", this->data_fname.c_str(), this->label.size() + 1, this->label.at(this->label.size() - 1).c_str());
        pclose(gp);
    }

    // (3) Setting for after the Second Time
    this->flag = true;

    // End Processing
    return;

}