package com.example.omnicompose.ui

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.example.omnicompose.data.CallSession
import com.example.omnicompose.data.UserProfile
import com.example.omnicompose.data.UserRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

class MainViewModel(private val repository: UserRepository) : ViewModel() {
    private val _users = MutableStateFlow<List<UserProfile>>(emptyList())
    val users: StateFlow<List<UserProfile>> = _users.asStateFlow()

    private val _randomMatch = MutableStateFlow<UserProfile?>(null)
    val randomMatch: StateFlow<UserProfile?> = _randomMatch.asStateFlow()

    private val _mockConversations = MutableStateFlow<List<CallSession>>(emptyList())
    val mockConversations: StateFlow<List<CallSession>> = _mockConversations.asStateFlow()

    init {
        viewModelScope.launch {
            repository.users().collect { profiles ->
                _users.value = profiles
                _mockConversations.value = profiles.take(2).mapIndexed { index, profile ->
                    CallSession(sessionId = "s$index", callee = profile, isIncoming = index % 2 == 0)
                }
            }
        }
    }

    fun refreshNearby(city: String) {
        viewModelScope.launch { repository.refreshNearby(city) }
    }

    fun findRandom() {
        viewModelScope.launch { _randomMatch.value = repository.getRandomMatch() }
    }

    fun startVerification() {
        // Hook up to your backend: upload selfie/video, request verification session, etc.
    }

    fun openProfile(profile: UserProfile) {
        // Navigate to detail screen or open bottom sheet with profile detail.
    }

    fun openChat(profile: UserProfile) {
        // Route to chat UI with messaging + call controls.
    }

    class Factory(private val repository: UserRepository) : ViewModelProvider.Factory {
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            if (modelClass.isAssignableFrom(MainViewModel::class.java)) {
                @Suppress("UNCHECKED_CAST")
                return MainViewModel(repository) as T
            }
            throw IllegalArgumentException("Unknown ViewModel")
        }
    }
}
