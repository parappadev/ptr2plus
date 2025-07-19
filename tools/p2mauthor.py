#p2m author by pips
#version 4.0
#version format = [p2m_version].[p2mauthor_update] e.g 3.1 = second version of p2mauthor for p2m version 3
import os
import re

print("Welcome to P2M Author by pips - version 3.0")

#TODO add sanitisation
title = input("Enter title of mod: ");
author = input("Enter author of mod: ");
desc = input("Enter description of mod: ");
print("Please prepare a folder with the modded files only, keeping their original filenames.")
inputDir = input("Enter path to mod folder(leave empty for none): ");
files = [];
if (inputDir != ""):
    while not os.path.isdir(inputDir):
        inputDir = inputDir.strip('"')
    if not os.access(inputDir, os.R_OK):
        input("Unable to read the input folder")
        sys.exit(1)

    inputDir = os.fsencode(inputDir)
    
    for folder in os.listdir(inputDir):
        print(folder.decode('UTF-8'))
        folderPath = os.path.join(inputDir, folder);
        if not os.path.isdir( folderPath ): #if not a folder
            input("WARNING: file " + folder.decode('UTF-8') + " is not in a folder, if this is intentional, press enter to continue");  
            files.append(folderPath)
        else:
            for path, subdirs, foundfiles in os.walk(folderPath):
                for filename in foundfiles:
                    print(foundfiles);
                    if (filename[-3:].upper() == b"INT"):
                        input("WARNING: file " + filename.decode('UTF-8') + " was found - We recommend using extracted INT files instead of the whole archive!, press enter to continue");  
                    files.append(os.path.join(path, filename))
    print("Files discovered:")
    print(files)

texInputDir = input("Enter path to texture replacements folder(leave empty for none): ");
texFiles = [];
if (texInputDir != ""):
    while not os.path.isdir(texInputDir):
        texInputDir = texInputDir.strip('"')
    if not os.access(texInputDir, os.R_OK):
        input("Unable to read the input folder")
        sys.exit(1)
    texInputDir = os.fsencode(texInputDir)
    texFiles = [];
    for path, subdirs, foundfiles in os.walk(texInputDir):
        for filename in foundfiles:
            print(foundfiles);
            if (filename[-3:].upper() != b"PNG" and filename[-3:].upper() != b"DDS"):
                print(filename[-3:].upper());
                input("WARNING: Skipped file " + filename.decode('UTF-8') + " due to it not being a PNG or DDS. Press enter to continue");  
            else:
                texFiles.append(os.path.join(path, filename))
    print("Texture Replacement Files discovered:")
    print(texFiles)

p2m_magic = b"\x50\x32\x4D\x11"
p2m_version = 4

title_size = len(title)
author_size = len(author)
desc_size = len(desc)

file_count = len(files)
tex_file_count = len(texFiles)

def add_bytes (buf, pos, bytes): 
    newpos = pos+len(bytes)
    buf[pos:newpos] = bytes;
    return newpos

def align (size, align):
    if (size % align != 0):
        return (align - (size % align))
    else:
        return 0
filename = re.sub(r"[/\\?%*:|\"<>\x7F\x00-\x1F]", "-", title)
filename += ".p2m"
with open(os.path.join(os.path.dirname(os.path.realpath(__file__)), filename), "wb") as output_file:
    
    #create header
    p2m_header = bytearray(0x50)
    #print(len(p2m_header))
    #print(p2m_header)
    p2m_header[0x00:0x04] = p2m_magic
    #print(p2m_header)
    p2m_header[0x04:0x06] = p2m_version.to_bytes(2, "little")
    
    p2m_header[0x08:0x0C] = file_count.to_bytes(4, "little")
    
    p2m_header[0x0C:0x10] = tex_file_count.to_bytes(4, "little")
    #print(p2m_header)
    
    #create meta chunk
    meta_size = title_size + author_size + desc_size + 3
    padding = align(meta_size, 0x10)
    meta_size += padding
    
    meta_chunk = bytearray(meta_size);
    
    pos = 0;
    pos = add_bytes(meta_chunk, pos, title_size.to_bytes(1,"little") )
    pos = add_bytes(meta_chunk, pos, str.encode(title))
    pos = add_bytes(meta_chunk, pos, author_size.to_bytes(1,"little") )
    pos = add_bytes(meta_chunk, pos, str.encode(author))
    pos = add_bytes(meta_chunk, pos, desc_size.to_bytes(1,"little") )
    pos = add_bytes(meta_chunk, pos, str.encode(desc))
    pos = add_bytes(meta_chunk, pos, (0).to_bytes(padding,"little") )
    
    #create path chunk
    path_size = 0
    for file in files:
        path = os.path.relpath(file, inputDir)
        path_size += len(path)
        path_size += 1
    padding = align(path_size, 0x10)
    path_size += padding
    path_chunk = bytearray(path_size);
    path_pos = 0
    
    for file in files:
        path = os.path.relpath(file, inputDir)
        path = (path.decode('UTF-8')).replace("/", "\\") #ptr2 uses forward slashes
        path_pos = add_bytes(path_chunk, path_pos, (len(path)).to_bytes(1,"little"))
        path_pos = add_bytes(path_chunk, path_pos, path.encode())
    path_pos = add_bytes(path_chunk, path_pos, (0).to_bytes(padding,"little"))
    
    #create type chunk
    type_size = file_count * 2
    padding = align(type_size, 0x10)
    type_size += padding
    type_chunk = bytearray(type_size)
    type_pos = 0
    for file in files:
        modType = 1
        if (file[-3:].upper() == b"WP2" or file[-3:].upper() == b"INT" or file[-3:].upper() == b"XTR" or file[-3:].upper() == b"OLM"):
            modType = 0
        #if (file[-3:].upper() == "PNG" or file[-3:].upper() == "DDS"):
        #    modType = 2
        type_pos = add_bytes(type_chunk, type_pos, modType.to_bytes(2,"little"))
    type_pos = add_bytes(type_chunk, type_pos, (0).to_bytes(padding,"little"))
    
    
    
    #create sizepos chunk
    sizepos_size = file_count * 8
    padding = align(sizepos_size, 0x10)
    sizepos_size += padding
    sizepos_chunk = bytearray(sizepos_size)
    
    file_no = 0
    files_size = 0
    for file in files:
        stats = os.stat(file)
        files_size += stats.st_size
        padding = align(stats.st_size, 0x10)
        files_size += padding
        #add size
        add_bytes(sizepos_chunk, (file_no * 8), (stats.st_size + padding).to_bytes(4, "little"))
        file_no+=1
    
    #files chunk
    files_chunk = bytearray(files_size)
    files_pos = 0
    file_no = 0
    
    fileschunk_pos = len(p2m_header + meta_chunk + path_chunk + type_chunk + sizepos_chunk)
    for file in files:
    
        #add pointer to sizepos
        add_bytes(sizepos_chunk, (file_no * 8) + 4, (files_pos + fileschunk_pos).to_bytes(4, "little"))
        
        f = open(file, mode="rb")
        file_data = f.read()
        files_pos = add_bytes(files_chunk, files_pos, file_data)
        padding = align(len(file_data), 0x10)
        files_pos = add_bytes(files_chunk, files_pos, (0).to_bytes(padding,"little"))
        
        
        file_no+=1
    
    #create tex_path chunk
    path_size = 0
    for file in texFiles:
        path = os.path.relpath(file, texInputDir)
        path_size += len(path)
        path_size += 1
    padding = align(path_size, 0x10)
    path_size += padding
    texpath_chunk = bytearray(path_size);
    path_pos = 0
    
    for file in texFiles:
        path = os.path.relpath(file, texInputDir)
       #path = (path.decode('UTF-8')).replace("/", "\\") #ptr2 uses forward slashes
        path_pos = add_bytes(texpath_chunk, path_pos, (len(path)).to_bytes(1,"little"))
        path_pos = add_bytes(texpath_chunk, path_pos, path)
    path_pos = add_bytes(texpath_chunk, path_pos, (0).to_bytes(padding,"little"))
    
    #create tex_sizepos chunk
    sizepos_size = tex_file_count * 8
    padding = align(sizepos_size, 0x10)
    sizepos_size += padding
    texsizepos_chunk = bytearray(sizepos_size)
    
    file_no = 0
    files_size = 0
    for file in texFiles:
        stats = os.stat(file)
        files_size += stats.st_size
        #padding = align(stats.st_size, 0x10)
        #files_size += padding
        #add size
        add_bytes(texsizepos_chunk, (file_no * 8), (stats.st_size).to_bytes(4, "little"))
        file_no+=1
        
    #texfiles chunk
    texfiles_chunk = bytearray(files_size)
    files_pos = 0
    file_no = 0
    
    fileschunk_pos = len(p2m_header + meta_chunk + path_chunk + type_chunk + sizepos_chunk + files_chunk + texpath_chunk + texsizepos_chunk)
    for file in texFiles:
    
        #add pointer to sizepos
        add_bytes(texsizepos_chunk, (file_no * 8) + 4, (files_pos + fileschunk_pos).to_bytes(4, "little"))
        
        f = open(file, mode="rb")
        file_data = f.read()
        files_pos = add_bytes(texfiles_chunk, files_pos, file_data)
        #padding = align(len(file_data), 0x10)
        #files_pos = add_bytes(texfiles_chunk, files_pos, (0).to_bytes(padding,"little"))
        
        
        file_no+=1
    
    
    
    #update header
    p2m_pos = 0
    meta_pos = len(p2m_header)
    path_pos = meta_pos + len(meta_chunk)
    type_pos = path_pos + len(path_chunk)
    sizepos_pos = type_pos + len(type_chunk)
    fileschunk_pos = sizepos_pos + len(sizepos_chunk)
    texpath_pos = fileschunk_pos + len(files_chunk)
    texsizepos_pos = texpath_pos + len(texpath_chunk)
    texfileschunk_pos = texsizepos_pos + len(texsizepos_chunk)
    
    add_bytes(p2m_header, 0x10, meta_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x14, len(meta_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x18, path_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x1C, len(path_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x20, type_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x24, len(type_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x28, sizepos_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x2C, len(sizepos_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x30, fileschunk_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x34, len(files_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x38, texpath_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x3C, len(texpath_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x40, texsizepos_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x44, len(texsizepos_chunk).to_bytes(4, "little"))
    add_bytes(p2m_header, 0x48, texfileschunk_pos.to_bytes(4, "little"))
    add_bytes(p2m_header, 0x4C, len(texfiles_chunk).to_bytes(4, "little"))
    
    
    
    output_file.write(p2m_header + meta_chunk + path_chunk + type_chunk + sizepos_chunk + files_chunk + texpath_chunk + texsizepos_chunk + texfiles_chunk)
    
input("Finished. Press enter to exit.")
    