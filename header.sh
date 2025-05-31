cd src
for file in *.h; do
    echo "/*******************************************************************************" > temp
    echo " $file - " >> temp
    echo "" >> temp
    echo " SPDX-License-Identifier: CC0 1.0 Universal Public Domain" >> temp
    echo " Original author Iain Patterson released nssm under Public Domain" >> temp
    echo " https://creativecommons.org/publicdomain/zero/1.0/" >> temp
    echo "" >> temp
    echo " NSSM source code - the Non-Sucking Service Manager" >> temp
    echo "" >> temp
    echo " 2025-05-31 and onwards modified Jerker Bäck" >> temp
    echo "" >> temp
    echo "*******************************************************************************/" >> temp
    echo "" >> temp
    echo "#pragma once" >> temp
    echo "" >> temp
    cat "$file" >> temp
    mv temp "$file"
done
for file in *.cpp; do
    echo "/*******************************************************************************" > temp
    echo " $file - " >> temp
    echo "" >> temp
    echo " SPDX-License-Identifier: CC0 1.0 Universal Public Domain" >> temp
    echo " Original author Iain Patterson released nssm under Public Domain" >> temp
    echo " https://creativecommons.org/publicdomain/zero/1.0/" >> temp
    echo "" >> temp
    echo " NSSM source code - the Non-Sucking Service Manager" >> temp
    echo "" >> temp
    echo " 2025-05-31 and onwards modified Jerker Bäck" >> temp
    echo "" >> temp
    echo "*******************************************************************************/" >> temp
    echo "" >> temp
    echo "#include \"nssm_pch.h\"" >> temp
    echo "#include \"common.h\"" >> temp
    echo "" >> temp    
    cat "$file" >> temp
    mv temp "$file"
done
cd ..
