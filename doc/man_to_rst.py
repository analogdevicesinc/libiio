# Convert man pages to reStructuredText
import os

manfolder = os.path.join(os.path.dirname(__file__), "..","build", "share","man")
page = ['iio_attr','iio_info','iio_genxml','iio_rwdev','iio_reg','iio_stresstest']

tools = {}

for po in page:
    print(f"Converting {po} to reStructuredText")
    p = po + ".1"
    fullpath = os.path.join(manfolder, p)
    target = os.path.join("source", "tools", po + ".rst")
    os.system(f"pandoc --from man --to rst {fullpath} -o {target}")

    # Add title to rst file
    with open(target, 'r') as file:
        data = file.readlines()

    # Downgrade all headers by one level
    for i in range(len(data)):
        if data[i].startswith("="):
            data[i] = data[i].replace("=", "-")
        if data[i].startswith("#"):
            data[i] = data[i].replace("#", "=")

    data.insert(0, f"{po}\n")
    data.insert(1, "=" * len(po) + "\n")

    # Remove NAME line and the following line
    for i in range(len(data)):
        if data[i].startswith("NAME"):
            data.pop(i)
            data.pop(i)
            ref = data[i+1]
            print(f"Reference: {ref}")
            tool = ref.split("-")[0].strip()
            description = ref.split("-")[1].strip()
            tools[tool] = description
            break

    with open(target, 'w') as file:
        file.writelines(data)

# Build index for cli tools
print("Building index for cli tools")

index = os.path.join("source", "tools", "index.rst")

with open(index, 'w') as file:
    file.write("Command Line Tools\n")
    file.write("==================\n\n")
    file.write(".. toctree::\n")
    file.write("   :maxdepth: 1\n\n")
    for tool in tools:
        file.write(f"   {tool}\n")
    file.write("\n\n")

