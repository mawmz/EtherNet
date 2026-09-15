# forces -fPIC into the compile args of an external target
macro(ethernet_external_fpic target)
    target_compile_options(${target} PUBLIC
            -fPIC -enable-libstdcxx-allocator=new
            )
endmacro()