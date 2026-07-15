on waitForStatus(vmReference, expectedStatus, attempts)
    tell application "UTM"
        repeat attempts times
            if status of vmReference is expectedStatus then return true
            delay 0.25
        end repeat
    end tell
    return false
end waitForStatus

on waitForCloneReady(vmName, attempts)
    tell application "UTM"
        repeat attempts times
            try
                set vmReference to virtual machine named vmName
                if backend of vmReference is qemu then
                    set configRecord to configuration of vmReference
                    set driveRecords to drives of configRecord
                    if (length of driveRecords) is 1 then return true
                end if
            end try
            delay 0.25
        end repeat
    end tell
    return false
end waitForCloneReady

on run arguments
    if (count of arguments) is not 3 then error "expected VM name, image path, and replacement name"

    set vmName to item 1 of arguments
    set imagePath to item 2 of arguments
    set replacementName to item 3 of arguments
    set imageFile to POSIX file imagePath

    tell application "UTM"
        set oldVM to missing value
        set matchingCount to 0
        repeat with candidateVM in (get every virtual machine)
            if name of candidateVM is vmName then
                set oldVM to candidateVM
                set matchingCount to matchingCount + 1
            end if
            if name of candidateVM is replacementName then
                error "temporary replacement VM already exists: " & replacementName
            end if
        end repeat

        if matchingCount is 0 then error "UTM VM not found: " & vmName
        if matchingCount is greater than 1 then error "multiple UTM VMs have the exact name: " & vmName
        if backend of oldVM is not qemu then error "VM must use the QEMU backend: " & vmName

        if status of oldVM is not stopped then
            stop oldVM by force
            if not my waitForStatus(oldVM, stopped, 40) then
                stop oldVM by kill
                if not my waitForStatus(oldVM, stopped, 40) then
                    error "could not stop UTM VM: " & vmName
                end if
            end if
        end if

        duplicate oldVM with properties {configuration:{name:replacementName}}
        try
            if not my waitForCloneReady(replacementName, 80) then
                error "replacement VM configuration did not become ready: " & replacementName
            end if
            set replacementConfig to configuration of virtual machine named replacementName

            if architecture of replacementConfig is not "x86_64" then
                error "VM architecture must be x86_64"
            end if
            set replacementDrives to drives of replacementConfig
            if (length of replacementDrives) is not 1 then
                error "VM must have exactly one drive"
            end if

            set replacementDrive to item 1 of replacementDrives
            if interface of replacementDrive is not IDE then
                error "the only VM drive must use the IDE interface"
            end if
            set replacementDriveId to id of replacementDrive
            set item 1 of drives of replacementConfig to {id:replacementDriveId, source:imageFile}
            update configuration of virtual machine named replacementName with replacementConfig
        on error errorMessage number errorNumber
            try
                delete virtual machine named replacementName
            end try
            error errorMessage number errorNumber
        end try

        delete oldVM

        set finalConfig to configuration of virtual machine named replacementName
        set name of finalConfig to vmName
        update configuration of virtual machine named replacementName with finalConfig
        set finalVM to virtual machine named vmName
        start finalVM

        if not my waitForStatus(finalVM, started, 80) then
            error "replacement VM was created but did not reach started state: " & vmName
        end if

        return id of finalVM
    end tell
end run
