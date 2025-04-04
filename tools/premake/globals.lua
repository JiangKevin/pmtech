-- setup global variables to configure the projects
platform_dir = ""
platform = ""
build_cmd = ""
link_cmd = ""
renderer_dir = ""
sdk_version = ""
shared_libs_dir = ""
pmtech_dir = "../"

-- setup functions
function setup_from_options()
    if _OPTIONS["renderer"] then
        renderer_dir = _OPTIONS["renderer"]
    end

    if _OPTIONS["sdk_version"] then
        sdk_version = _OPTIONS["sdk_version"]
    end

    if _OPTIONS["platform_dir"] then
        platform_dir = _OPTIONS["platform_dir"]
    end

    if _OPTIONS["toolset"] then
        toolset(_OPTIONS["toolset"])
    end

    if _OPTIONS["pmtech_dir"] then
        pmtech_dir = _OPTIONS["pmtech_dir"]
    end
end

function script_path()
   local str = debug.getinfo(2, "S").source:sub(2)
   return str:match("(.*/)")
end

function windows_sdk_version()
    if sdk_version ~= "" then
        return sdk_version
    end
    return "10.0.16299.0"
end

function setup_curl()
    -- includes
    includedirs {
        ("../../third_party/libcurl/include")
    }

    -- lib dirs
    libdirs {
        ("../../third_party/libcurl/lib/" .. platform)
    }

    -- links
    if platform == "ios" then
        links {
            "curl",
            "Security.framework",
            "z"
        }
    elseif platform == "win32" then
        links {
            "libcurl",
            "libssl",
            "libcrypto",
            -- win32 libs required for curl
            "ws2_32",
            "gdi32",
            "advapi32",
            "crypt32",
            "user32"

        }
    else
        links {
            "curl",
            "ssl",
            "crypto" -- crypto must follow 'ssl' for single pass linkers
        }
    end
end


function setup_from_action()
    if _ACTION == "gmake" then
        if platform_dir == "web" then
            build_cmd = "-std=c++11 -s WASM=1 -s INITIAL_MEMORY=1024MB -s DETERMINISTIC=0 -s PTHREAD_POOL_SIZE=8"
            link_cmd = "-s --shared-memory -s WASM=1 -s FULL_ES3=1 -s MIN_WEBGL_VERSION=2 -s MAX_WEBGL_VERSION=2 -s PTHREAD_POOL_SIZE=8 -s INITIAL_MEMORY=1024MB --shell-file ../../../core/template/web/shell.html"            
        elseif platform_dir == "linux" then
            build_cmd = "-std=c++11 -mfma -mavx -mavx2 -msse2"
        else -- macos
            build_cmd = "-std=c++11 -stdlib=libc++ -mfma -mavx -mavx2 -msse2"
            link_cmd = "-stdlib=libc++"
        end
    elseif _ACTION == "xcode4" then 
        platform_dir = "osx" 
        if not renderer_dir then
            renderer_dir = "opengl"
        end
        if _OPTIONS["xcode_target"] then
            platform_dir = _OPTIONS["xcode_target"]
        end
        if platform_dir == "ios" then
            build_cmd = "-std=c++11 -stdlib=libc++"
            link_cmd = "-stdlib=libc++"
        else
            build_cmd = "-std=c++11 -stdlib=libc++ -mfma -mavx -mavx2 -msse2"
            link_cmd = "-stdlib=libc++"
        end
    elseif _ACTION == "android-studio" then 
        build_cmd = { "-std=c++11" }
    elseif _ACTION == "vs2017" or _ACTION == "vs2019" or _ACTION == "vs2022" then
        platform_dir = "win32" 
        build_cmd = "/Ob1 /arch:AVX2 /arch:AVX " -- use force inline and avx
        disablewarnings { "4267", "4305", "4244" }
    end
    
    platform = platform_dir
    
    if platform == "win32" then
        shared_libs_dir = ("../../" .. pmtech_dir .. '/third_party/shared_libs/' .. platform_dir)
    elseif platform == "osx"  then
        shared_libs_dir = ( '"' .. "../../" .. pmtech_dir .. '/third_party/shared_libs/' .. platform_dir .. '/"' )
    end
    
    -- link curl for url fetching
    setup_curl()
    
    print("platform: " .. platform)
    print("renderer: " .. renderer_dir)
    print("pmtech dir: " .. pmtech_dir)
    print("sdk version: " .. windows_sdk_version())
    
end

-- setup product
function setup_product_ios(name)
    bundle_name = ("com.pmtech") 
    xcodebuildsettings {
        ["PRODUCT_BUNDLE_IDENTIFIER"] = bundle_name
    }
end

function setup_product(name)
    if platform == "ios" then setup_product_ios(name)
    end
end

-- setup env - inserts architecture, platform and sdk settings
function setup_env_ios()
    xcodebuildsettings {
        ["ARCHS"] = "$(ARCHS_STANDARD)",
        ["SDKROOT"] = "iphoneos"
    }
    if _OPTIONS["teamid"] then
        xcodebuildsettings {
            ["DEVELOPMENT_TEAM"] = _OPTIONS["teamid"]
        }
    end
end

function setup_env_osx()
    xcodebuildsettings {
        ["MACOSX_DEPLOYMENT_TARGET"] = "10.13"
    }
    architecture "x64"
end

function setup_env_win32()
    architecture "x64"
end

function setup_env_linux()
    architecture "x64"
end

function setup_env()
    if platform == "ios" then setup_env_ios()
    elseif platform == "osx" then setup_env_osx()
    elseif platform == "win32" then setup_env_win32()
    elseif platform == "linux" then setup_env_linux()
    end
end

-- setup platform defines - inserts platform defines for porting macros
function setup_platform_defines()
    defines
    {
        ("PEN_PLATFORM_" .. string.upper(platform)),
        ("PEN_RENDERER_" .. string.upper(renderer_dir))
    }
end

-- entry
setup_from_options()
setup_from_action()