#include "stdafx.h"
#include "Prog_info.hpp"
#include "Shell.hpp"
#include "Program.hpp"

int main(int argc, char *argv[])
{
    {
        bool enable_validation = true;
        bool use_culling = true;
        std::string model_filename = "cornell.fbx";
        if (argc > 1) enable_validation = strcmp(argv[1], "false");
        if (argc > 2) use_culling = strcmp(argv[2], "false");
        if (argc > 3) model_filename = argv[3];

        Prog_info prog_info{};
        base::Camera camera{};
        Shell shell{&prog_info, &camera};

        Program program{enable_validation, use_culling, &prog_info, &shell, &camera, model_filename};
        program.init();
        program.run();
    }
    printf("press any key...");
    getchar();
    return 0;
}