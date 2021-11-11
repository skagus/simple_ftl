@echo off
FOR /F "tokens=* USEBACKQ" %%F IN (`date_time`) DO (
	SET file_name=%%F
)
echo Zip file to %file_name%
7z.exe a -ttar -r %file_name%.gif @zip_list.txt
7z.exe a -tzip -psdump7!! -r SF_%file_name%.zip %file_name%.gif
del %file_name%.gif
