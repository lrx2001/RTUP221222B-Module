# RTUP221222B Module
This project corresponds to the testing software in section P221222B. Functionally, it primarily offers three features: data and curve display, and offline data viewing. Data changes can be observed on the curve within the data page. Offline data playback also supports viewing data from both the data page and the curve. Data is stored in SQLite format, with a maximum storage duration of 7 days.

This page is currently disconnected and contains no data.
![20260228105836](https://github.com/user-attachments/assets/06728b7b-6fd2-46fb-bb46-37bd208fd76f)

Once the connection is successful, you can access the data page by clicking the curve display button at the top or the curve button at the bottom of the page when you need a curve.
![20260228111247](https://github.com/user-attachments/assets/3573aca1-4b64-4301-956b-fdd13cf49f5f)

Opening the curve window will initially result in a blank screen; you'll need to manually select options to display the curve. Setting the curve to a non-modal window allows for convenient simultaneous viewing of data and the curve. After the program runs, you can configure the curve window to only open one at a time to prevent interference from multiple open curve windows.
![20260228111351](https://github.com/user-attachments/assets/efc4fcfe-b51c-4ee2-b67e-2772001c799f)

Clicking "Start Playback" will bring up a pop-up window where you can select the files for the playback data.
![20260228110211](https://github.com/user-attachments/assets/622896e3-0cde-403c-9253-aafed99b04fa)

After the data is imported, the page will display a sliding progress bar to view the data for a specific time period.
![20260228110519](https://github.com/user-attachments/assets/5fd19282-82eb-43f6-8b5b-db20802e23de)

For curves, you need to manually select the corresponding name to display the curve.
![20260228110921](https://github.com/user-attachments/assets/8555d01d-99ff-4e25-b2a6-2d6c26e07a0f)

The program automatically saves data when disconnected or closed. The saved data is automatically stored in the "Data" folder under the project file. When needed, a new folder is automatically created to store the data for the current day.
![20260228112157](https://github.com/user-attachments/assets/3f2d39f2-6604-4053-a9d5-ae70ebfcb3df)


