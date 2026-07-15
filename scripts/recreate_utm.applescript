on waitForStatus(vmReference, expectedStatus, attempts)
    tell application "UTM"
        repeat attempts times
            if status of vmReference is expectedStatus then return true
            delay 0.25
        end repeat
    end tell
    return false
end waitForStatus

on run arguments
    if (count of arguments) is not 3 then error "expected VM name, image path, and replacement name"

    set vmName to item 1 of arguments
    set imagePath to item 2 of arguments
    set replacementName to item 3 of arguments
    set imageFile to POSIX file imagePath
    set replacementVM to missing value
    set oldDeleted to false

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

        try
            set replacementVM to duplicate oldVM with properties {configuration:{name:replacementName}}
            set replacementConfig to configuration of replacementVM

            if architecture of replacementConfig is not "x86_64" then
                error "VM architecture must be x86_64"
            end if
            if (count of drives of replacementConfig) is not 1 then
                error "VM must have exactly one drive"
            end if

            set replacementDrive to item 1 of drives of replacementConfig
            if interface of replacementDrive is not IDE then
                error "the only VM drive must use the IDE interface"
            end if
            set replacementDriveId to id of replacementDrive
            set item 1 of drives of replacementConfig to {id:replacementDriveId, source:imageFile}
            update configuration of replacementVM with replacementConfig

            delete oldVM
            set oldDeleted to true

            set finalConfig to configuration of replacementVM
            set name of finalConfig to vmName
            update configuration of replacementVM with finalConfig
            start replacementVM

            if not my waitForStatus(replacementVM, started, 80) then
                error "replacement VM was created but did not reach started state: " & vmName
            end if

            return id of replacementVM
        on error errorMessage number errorNumber
            if replacementVM is not missing value and oldDeleted is false then
                try
                    delete replacementVM
                end try
            end if
            error errorMessage number errorNumber
        end try
    end tell
end run
