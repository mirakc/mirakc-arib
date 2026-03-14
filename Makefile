.PHONY: check
check: check-github-actions

.PHONY: check-github-actions
check-github-actions:
	zizmor -p .github

.PHONY: pin
pin: pin-github-actions

.PHONY: pin-github-actions
pin-github-actions:
	pinact run
