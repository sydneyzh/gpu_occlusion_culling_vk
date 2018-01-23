#include "stdafx.h"
#include "Prog_info.hpp"
#include "Shell.hpp"
#include "Program.hpp"

int main(int argc, char *argv[])
{
    {
        bool enable_validation = true;
        if (argc > 1) enable_validation = strcmp(argv[1], "false");
        std::string filename = "";
        if (argc > 2) filename = argv[2];

        Prog_info prog_info{};
        base::Camera camera{};
        Shell shell{&prog_info, &camera};

        Program program{enable_validation, &prog_info, &shell, &camera, filename};
        program.init();
        program.run();
    }
    printf("press any key...");
    getchar();
    return 0;
}