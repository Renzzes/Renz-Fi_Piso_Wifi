package com.renzfi.owner.ui.components

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.renzfi.owner.R
import com.renzfi.owner.util.Constants

@Composable
fun DeleteConfirmDialog(
    deviceName: String,
    onConfirm: () -> Unit,
    onDismiss: () -> Unit,
) {
    var confirmText by remember { mutableStateOf("") }
    val canDelete = confirmText == Constants.DELETE_CONFIRM_TEXT

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.delete_device)) },
        text = {
            Column {
                Text(
                    text = stringResource(R.string.delete_device_message, deviceName),
                    style = MaterialTheme.typography.bodyMedium,
                )
                Spacer(modifier = Modifier.height(16.dp))
                Text(
                    text = "Type ${Constants.DELETE_CONFIRM_TEXT} to confirm:",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurface.copy(alpha = 0.7f),
                )
                Spacer(modifier = Modifier.height(8.dp))
                OutlinedTextField(
                    value = confirmText,
                    onValueChange = { confirmText = it },
                    modifier = Modifier.fillMaxWidth(),
                    singleLine = true,
                    placeholder = { Text(Constants.DELETE_CONFIRM_TEXT) },
                )
            }
        },
        confirmButton = {
            Button(
                onClick = onConfirm,
                enabled = canDelete,
            ) {
                Text("Delete")
            }
        },
        dismissButton = {
            OutlinedButton(onClick = onDismiss) {
                Text("Cancel")
            }
        },
    )
}
