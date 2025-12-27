-- xmake.lua for SET-ISCA2023 project
-- STSchedule: AI Model Load Mapping Tool

-- 设置项目信息
set_project("STSchedule")
set_version("1.0.0")
set_xmakever("2.7.0")

-- 设置默认构建模式
add_rules("mode.debug", "mode.release", "mode.releasedbg")

-- 设置C++标准
set_languages("cxx17")

-- 设置警告级别
set_warnings("all", "extra")

-- 定义目标可执行文件
target("stschedule")
    -- 设置目标类型为可执行文件
    set_kind("binary")
    
    -- 添加源文件
    add_files("src/*.cpp")
    add_files("src/nns/*.cpp")
    add_files("src/json/*.cpp")
    
    -- 添加头文件搜索路径
    add_includedirs("include")
    
    -- 添加链接库
    add_syslinks("pthread", "m")
    
    -- 设置输出目录
    set_targetdir("$(builddir)")
    set_objectdir("$(builddir)/objects")
    
    -- Debug模式配置
    if is_mode("debug") then
        add_defines("DEBUG")
        set_symbols("debug")
        set_optimize("none")
    end
    
    -- Release模式配置
    if is_mode("release") then
        set_optimize("aggressive")
        set_strip("all")
    end
    
    -- ReleaseDbg模式配置（带调试信息的优化版本，用于性能分析）
    if is_mode("releasedbg") then
        set_optimize("aggressive")
        set_symbols("debug")
    end

-- 自定义任务：清理构建文件
task("clean-all")
    on_run(function ()
        import("core.base.option")
        os.rm("build")
        print("All build files cleaned.")
    end)
    set_menu {
        usage = "xmake clean-all",
        description = "Clean all build files including build directory"
    }

-- 自定义任务：显示项目信息
task("info")
    on_run(function ()
        import("core.project.config")
        import("core.project.project")
        
        print("[*] Project:         " .. project.name() or "STSchedule")
        print("[*] Version:         " .. (project.version() or "1.0.0"))
        print("[*] Build mode:      " .. (config.mode() or "release"))
        print("[*] Build directory: " .. (config.builddir() or "./build"))
        print("[*] C++ Standard:    c++17")
        
        local target = project.target("stschedule")
        if target then
            print("[*] Source files:")
            for _, sourcefile in ipairs(target:sourcefiles()) do
                print("    - " .. sourcefile)
            end
        end
    end)
    set_menu {
        usage = "xmake info",
        description = "Display project information"
    }
